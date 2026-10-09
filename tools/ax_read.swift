// A running client's UI as macOS accessibility clients read it (D579):
// the application of the process named is found, and its tree is read
// through the AX API until a button the pattern names is in it, or any
// button where none is given, a minute at most (the first asking makes the
// client's access, and the tree comes after it; the window's own buttons
// are there before it). Prints whether this process may read others
// ("ax: trusted"), then one line a node, "ax: <depth> <role> <title or
// description>". Given a pattern, the first button whose name matches it is
// pressed as a screen reader presses it: "ax: pressed <name>". Reading
// another application needs this process trusted for accessibility.
//
//   swift tools/ax_read.swift <process name> [<name pattern>]
import AppKit
import ApplicationServices

let arguments = CommandLine.arguments
guard arguments.count >= 2 else {
    print("usage: ax_read.swift <process name> [<name pattern>]")
    exit(2)
}
let name = arguments[1]
let pattern = arguments.count > 2 ? arguments[2] : ""
print("ax: trusted \(AXIsProcessTrusted())")

func attribute(_ element: AXUIElement, _ key: String) -> CFTypeRef? {
    var value: CFTypeRef?
    return AXUIElementCopyAttributeValue(element, key as CFString, &value) == .success ? value : nil
}

func nameOf(_ element: AXUIElement) -> String {
    for key in [kAXTitleAttribute, kAXDescriptionAttribute, kAXValueAttribute] {
        if let text = attribute(element, key as String) as? String, !text.isEmpty {
            return text
        }
    }
    return ""
}

// Each node's role and name, depth first, at most twelve deep.
func walk(_ element: AXUIElement, _ depth: Int, _ into: inout [(Int, String, String, AXUIElement)]) {
    let role = attribute(element, kAXRoleAttribute as String) as? String ?? "none"
    into.append((depth, role, nameOf(element), element))
    guard depth < 12, let children = attribute(element, kAXChildrenAttribute as String) as? [AXUIElement] else {
        return
    }
    for child in children {
        walk(child, depth + 1, &into)
    }
}

let deadline = Date().addingTimeInterval(60)
var nodes: [(Int, String, String, AXUIElement)] = []
while Date() < deadline {
    let running = NSWorkspace.shared.runningApplications.first {
        $0.executableURL?.lastPathComponent == name || $0.localizedName == name
    }
    if let running {
        nodes = []
        walk(AXUIElementCreateApplication(running.processIdentifier), 0, &nodes)
        if nodes.contains(where: {
            $0.1 == kAXButtonRole as String
                && (pattern.isEmpty || $0.2.range(of: pattern, options: .regularExpression) != nil)
        }) {
            break
        }
    }
    Thread.sleep(forTimeInterval: 0.5)
}
if nodes.isEmpty {
    print("ax: no application \(name)")
    exit(1)
}
for (depth, role, title, _) in nodes {
    print("ax: \(depth) \(role) \(title)")
}
if !pattern.isEmpty {
    guard let button = nodes.first(where: {
        $0.1 == kAXButtonRole as String && $0.2.range(of: pattern, options: .regularExpression) != nil
    }) else {
        print("ax: no button matching \(pattern)")
        exit(1)
    }
    let pressed = AXUIElementPerformAction(button.3, kAXPressAction as CFString)
    print("ax: pressed \(button.2): \(pressed == .success)")
    exit(pressed == .success ? 0 : 1)
}
exit(0)
