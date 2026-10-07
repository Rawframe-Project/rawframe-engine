#include "adapter.h"

#include <string_view>
#include <utility>

namespace rawframe::debug_adapter {

using document::Value;

namespace {

/// A member's text, or empty.
std::string textOf(const Value& object, std::string_view name) {
    const Value* kMember = object.kind() == Value::Kind::Object ? object.find(name) : nullptr;
    return kMember != nullptr && kMember->text() != nullptr ? *kMember->text() : std::string{};
}

/// The one thread a game shows: its tick.
constexpr std::int64_t kGameThread = 1;

} // namespace

Value Adapter::response(const Value& request, bool success, Value body, const std::string& message) {
    Value made = Value::object();
    made.add("seq", Value::integer(++seq_));
    made.add("type", Value::string("response"));
    const Value* kSeq = request.find("seq");
    made.add("request_seq", kSeq != nullptr ? *kSeq : Value::integer(0));
    made.add("success", Value::boolean(success));
    made.add("command", Value::string(textOf(request, "command")));
    if (!message.empty()) {
        made.add("message", Value::string(message));
    }
    if (!body.isNull()) {
        made.add("body", std::move(body));
    }
    return made;
}

Value Adapter::event(std::string name, Value body) {
    Value made = Value::object();
    made.add("seq", Value::integer(++seq_));
    made.add("type", Value::string("event"));
    made.add("event", Value::string(std::move(name)));
    if (!body.isNull()) {
        made.add("body", std::move(body));
    }
    return made;
}

std::optional<Value> Adapter::ask(Value record, std::string& why) {
    if (link_ == nullptr) {
        why = "not attached to a game";
        return std::nullopt;
    }
    record.add("id", Value::integer(++asked_));
    const std::optional<std::string> kReply = link_->ask(document::writeCompact(record));
    if (!kReply.has_value()) {
        why = "the game's tooling endpoint went";
        link_.reset();
        done_ = true;
        return std::nullopt;
    }
    auto parsed = document::parse(*kReply);
    const Value* kAnswer = parsed.has_value() ? parsed->find("answer") : nullptr;
    if (kAnswer == nullptr) {
        const Value* kError = parsed.has_value() ? parsed->find("error") : nullptr;
        why = kError != nullptr ? textOf(*kError, "message") : std::string{"the game did not answer"};
        return std::nullopt;
    }
    return *kAnswer;
}

std::vector<Value> Adapter::handle(const Value& request) {
    std::vector<Value> out;
    const std::string kCommand = textOf(request, "command");
    const Value* kArguments = request.find("arguments");
    const Value kNone = Value::object();
    const Value& arguments = kArguments != nullptr && kArguments->kind() == Value::Kind::Object ? *kArguments : kNone;
    std::string why;
    if (kCommand == "initialize") {
        Value body = Value::object();
        body.add("supportsConfigurationDoneRequest", Value::boolean(true));
        body.add("supportsFunctionBreakpoints", Value::boolean(true));
        out.push_back(response(request, true, std::move(body)));
    } else if (kCommand == "attach") {
        // The game's tooling endpoint, the certificate to trust, and the
        // token: what the endpoint's own settings name.
        const std::string kEndpoint = textOf(arguments, "endpoint");
        const std::string kPin = textOf(arguments, "pinFile");
        const std::string kToken = textOf(arguments, "tokenFile");
        std::string said;
        if (!kEndpoint.empty() && !kPin.empty() && !kToken.empty()) {
            link_ = authoring_session::ToolingLink::open(kEndpoint, kPin.c_str(), kToken.c_str(), said);
        }
        if (link_ == nullptr) {
            out.push_back(response(request,
                                   false,
                                   {},
                                   kEndpoint.empty() || kPin.empty() || kToken.empty()
                                       ? "attach names the game's endpoint, pinFile, and tokenFile"
                                       : "the game's tooling endpoint could not be reached: " + said));
        } else if (said.find("\"debug\"") == std::string::npos) {
            link_.reset();
            out.push_back(response(request, false, {}, "the game's tooling endpoint does not grant debug"));
        } else {
            out.push_back(response(request, true));
            out.push_back(event("initialized", {}));
        }
    } else if (kCommand == "setFunctionBreakpoints") {
        // Each name asked alone, to say which are functions, then all.
        std::vector<std::string> names;
        const Value* kBreakpoints = arguments.find("breakpoints");
        for (const Value& each : kBreakpoints != nullptr && kBreakpoints->kind() == Value::Kind::Array
                                     ? kBreakpoints->items()
                                     : std::span<const Value>{}) {
            names.push_back(textOf(each, "name"));
        }
        Value verified = Value::array();
        bool reached = true;
        for (const std::string& kName : names) {
            Value alone = Value::object();
            alone.add("kind", Value::string("debug.break"));
            alone.add("functions", Value::array({Value::string(kName)}));
            const std::optional<Value> kFound = reached ? ask(std::move(alone), why) : std::nullopt;
            reached = kFound.has_value();
            const Value* kCount = kFound.has_value() ? kFound->find("found") : nullptr;
            Value breakpoint = Value::object();
            const bool kFunction = kCount != nullptr && kCount->integer().value_or(0) > 0;
            breakpoint.add("verified", Value::boolean(kFunction));
            if (!kFunction) {
                breakpoint.add("message", Value::string(reached ? "no function of the game is named so" : why));
            }
            verified.push(std::move(breakpoint));
        }
        Value all = Value::object();
        all.add("kind", Value::string("debug.break"));
        Value functions = Value::array();
        for (const std::string& kName : names) {
            functions.push(Value::string(kName));
        }
        all.add("functions", std::move(functions));
        if (reached && !ask(std::move(all), why).has_value()) {
            reached = false;
        }
        Value body = Value::object();
        body.add("breakpoints", std::move(verified));
        out.push_back(response(request, reached, std::move(body), reached ? std::string{} : why));
    } else if (kCommand == "setBreakpoints") {
        // A line is a place Kest's public header cannot yet name (D460).
        const Value* kBreakpoints = arguments.find("breakpoints");
        Value refused = Value::array();
        for (std::size_t each = 0; kBreakpoints != nullptr && kBreakpoints->kind() == Value::Kind::Array &&
                                   each < kBreakpoints->items().size();
             ++each) {
            Value breakpoint = Value::object();
            breakpoint.add("verified", Value::boolean(false));
            breakpoint.add("message", Value::string("a game breaks at functions for now: add a function breakpoint"));
            refused.push(std::move(breakpoint));
        }
        Value body = Value::object();
        body.add("breakpoints", std::move(refused));
        out.push_back(response(request, true, std::move(body)));
    } else if (kCommand == "setExceptionBreakpoints" || kCommand == "configurationDone") {
        out.push_back(response(request, true));
    } else if (kCommand == "threads") {
        Value thread = Value::object();
        thread.add("id", Value::integer(kGameThread));
        thread.add("name", Value::string("game"));
        Value body = Value::object();
        body.add("threads", Value::array({std::move(thread)}));
        out.push_back(response(request, true, std::move(body)));
    } else if (kCommand == "stackTrace") {
        Value status = Value::object();
        status.add("kind", Value::string("debug.status"));
        const std::optional<Value> kStatus = ask(std::move(status), why);
        const Value* kFrames = kStatus.has_value() ? kStatus->find("frames") : nullptr;
        frames_.clear();
        Value stack = Value::array();
        for (const Value& each : kFrames != nullptr && kFrames->kind() == Value::Kind::Array
                                     ? kFrames->items()
                                     : std::span<const Value>{}) {
            frames_.push_back(each);
            Value frame = Value::object();
            frame.add("id", Value::integer(static_cast<std::int64_t>(frames_.size())));
            frame.add("name", Value::string(textOf(each, "function")));
            frame.add("line", Value::integer(0));
            frame.add("column", Value::integer(0));
            stack.push(std::move(frame));
        }
        Value body = Value::object();
        body.add("totalFrames", Value::integer(static_cast<std::int64_t>(frames_.size())));
        body.add("stackFrames", std::move(stack));
        out.push_back(response(request, kStatus.has_value(), std::move(body), why));
    } else if (kCommand == "scopes") {
        // One scope a frame, its arguments: the locals a stop surely wrote.
        const Value* kFrame = arguments.find("frameId");
        const std::int64_t kId = kFrame != nullptr ? kFrame->integer().value_or(0) : 0;
        Value scope = Value::object();
        scope.add("name", Value::string("Arguments"));
        scope.add("variablesReference", Value::integer(kId));
        scope.add("expensive", Value::boolean(false));
        Value body = Value::object();
        body.add("scopes", Value::array({std::move(scope)}));
        out.push_back(response(request, kId >= 1 && static_cast<std::size_t>(kId) <= frames_.size(), std::move(body)));
    } else if (kCommand == "variables") {
        const Value* kReference = arguments.find("variablesReference");
        const std::int64_t kId = kReference != nullptr ? kReference->integer().value_or(0) : 0;
        Value variables = Value::array();
        const Value* kLocals = kId >= 1 && static_cast<std::size_t>(kId) <= frames_.size()
                                   ? frames_[static_cast<std::size_t>(kId - 1)].find("locals")
                                   : nullptr;
        for (const Value& each : kLocals != nullptr && kLocals->kind() == Value::Kind::Array
                                     ? kLocals->items()
                                     : std::span<const Value>{}) {
            Value variable = Value::object();
            variable.add("name", Value::string(textOf(each, "name")));
            variable.add("value", Value::string(textOf(each, "value")));
            variable.add("variablesReference", Value::integer(0));
            variables.push(std::move(variable));
        }
        Value body = Value::object();
        body.add("variables", std::move(variables));
        out.push_back(response(request, true, std::move(body)));
    } else if (kCommand == "continue") {
        Value carry = Value::object();
        carry.add("kind", Value::string("debug.continue"));
        const bool kCarried = ask(std::move(carry), why).has_value();
        told_ = false;
        frames_.clear();
        Value body = Value::object();
        body.add("allThreadsContinued", Value::boolean(true));
        out.push_back(response(request, kCarried, std::move(body), kCarried ? std::string{} : why));
    } else if (kCommand == "disconnect") {
        // The game is left as it was before: no breakpoints, and running.
        if (link_ != nullptr) {
            Value none = Value::object();
            none.add("kind", Value::string("debug.break"));
            none.add("functions", Value::array());
            static_cast<void>(ask(std::move(none), why));
            Value carry = Value::object();
            carry.add("kind", Value::string("debug.continue"));
            static_cast<void>(ask(std::move(carry), why));
        }
        link_.reset();
        done_ = true;
        out.push_back(response(request, true));
    } else if (kCommand == "next" || kCommand == "stepIn" || kCommand == "stepOut" || kCommand == "pause") {
        out.push_back(response(request,
                               false,
                               {},
                               "a game is stepped once Kest's public header says where code was written; continue "
                               "to the next function breakpoint"));
    } else {
        out.push_back(response(request, false, {}, "this adapter does not answer " + kCommand));
    }
    return out;
}

std::vector<Value> Adapter::poll() {
    std::vector<Value> out;
    if (link_ == nullptr) {
        if (done_ && !terminated_) {
            terminated_ = true;
            out.push_back(event("terminated", {}));
        }
        return out;
    }
    std::string why;
    Value status = Value::object();
    status.add("kind", Value::string("debug.status"));
    const std::optional<Value> kStatus = ask(std::move(status), why);
    if (!kStatus.has_value()) {
        if (link_ == nullptr && !terminated_) {
            terminated_ = true;
            out.push_back(event("terminated", {}));
        }
        return out;
    }
    const Value* kStopped = kStatus->find("stopped");
    const bool kIsStopped = kStopped != nullptr && kStopped->truth().value_or(false);
    if (kIsStopped && !told_) {
        told_ = true;
        Value body = Value::object();
        body.add("reason", Value::string("function breakpoint"));
        body.add("threadId", Value::integer(kGameThread));
        body.add("allThreadsStopped", Value::boolean(true));
        out.push_back(event("stopped", std::move(body)));
    } else if (!kIsStopped) {
        told_ = false;
    }
    return out;
}

} // namespace rawframe::debug_adapter
