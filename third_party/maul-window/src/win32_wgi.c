// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32's Xbox gamepads through Windows.Gaming.Input.

#include "win32_wgi.h"

#include "maul-unicode/encoding.h"

#include <objbase.h>
#include <string.h>

// The first six methods of every runtime interface: IUnknown's, taking
// the object whatever its interface, then IInspectable's, which nothing
// here calls.
#define MWIN_INSPECTABLE                                                                           \
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(void* self, REFIID iid, void** object);             \
    ULONG(STDMETHODCALLTYPE* AddRef)(void* self);                                                  \
    ULONG(STDMETHODCALLTYPE* Release)(void* self);                                                 \
    void* GetIids;                                                                                 \
    void* GetRuntimeClassName;                                                                     \
    void* GetTrustLevel;

// The buttons of a reading, as the runtime numbers them.
enum
{
    wgiMenu = 0x1,
    wgiView = 0x2,
    wgiA = 0x4,
    wgiB = 0x8,
    wgiX = 0x10,
    wgiY = 0x20,
    wgiDpadUp = 0x40,
    wgiDpadDown = 0x80,
    wgiDpadLeft = 0x100,
    wgiDpadRight = 0x200,
    wgiShoulderLeft = 0x400,
    wgiShoulderRight = 0x800,
    wgiStickLeft = 0x1000,
    wgiStickRight = 0x2000,
};

// The runtime's buttons in mwinGamepadButton order; it gives no guide
// button.
static const uint32_t s_buttons[MWIN_GAMEPAD_BUTTONS] = {
    wgiDpadUp,       wgiDpadDown,      wgiDpadLeft,  wgiDpadRight,  wgiA,    wgiB,    wgiX, wgiY,
    wgiShoulderLeft, wgiShoulderRight, wgiStickLeft, wgiStickRight, wgiMenu, wgiView, 0,
};

// The runtime's structs, as it lays them out.
typedef struct Token
{
    int64_t value;
} Token;

typedef struct Reading
{
    UINT64 timestamp;
    INT32 buttons;
    double leftTrigger;
    double rightTrigger;
    double leftX;
    double leftY;
    double rightX;
    double rightY;
} Reading;

typedef struct Vibration
{
    double leftMotor;
    double rightMotor;
    double leftTrigger;
    double rightTrigger;
} Vibration;

typedef struct Object Object;
typedef struct Gamepad Gamepad;
typedef struct GamepadList GamepadList;
typedef struct GamepadStatics GamepadStatics;
typedef struct RawController RawController;
typedef struct RawController2 RawController2;
typedef struct RawStatics RawStatics;
typedef struct BatteryInfo BatteryInfo;
typedef struct BatteryReport BatteryReport;
typedef struct IntReference IntReference;

struct Object
{
    const struct
    {
        MWIN_INSPECTABLE
    }* v;
};

struct Gamepad
{
    const struct
    {
        MWIN_INSPECTABLE
        void* getVibration;
        HRESULT(STDMETHODCALLTYPE* setVibration)(Gamepad* self, Vibration value);
        HRESULT(STDMETHODCALLTYPE* read)(Gamepad* self, Reading* value);
    }* v;
};

struct GamepadList
{
    const struct
    {
        MWIN_INSPECTABLE
        HRESULT(STDMETHODCALLTYPE* at)(GamepadList* self, UINT32 index, Gamepad** value);
        HRESULT(STDMETHODCALLTYPE* size)(GamepadList* self, UINT32* value);
    }* v;
};

struct GamepadStatics
{
    const struct
    {
        MWIN_INSPECTABLE
        HRESULT(STDMETHODCALLTYPE* addAdded)(GamepadStatics* self, mwinWgiHandler* handler,
                                             Token* token);
        HRESULT(STDMETHODCALLTYPE* removeAdded)(GamepadStatics* self, Token token);
        HRESULT(STDMETHODCALLTYPE* addRemoved)(GamepadStatics* self, mwinWgiHandler* handler,
                                               Token* token);
        HRESULT(STDMETHODCALLTYPE* removeRemoved)(GamepadStatics* self, Token token);
        HRESULT(STDMETHODCALLTYPE* gamepads)(GamepadStatics* self, GamepadList** value);
    }* v;
};

struct RawController
{
    const struct
    {
        MWIN_INSPECTABLE
        void* axisCount;
        void* buttonCount;
        void* motors;
        HRESULT(STDMETHODCALLTYPE* product)(RawController* self, UINT16* value);
        HRESULT(STDMETHODCALLTYPE* vendor)(RawController* self, UINT16* value);
    }* v;
};

struct RawController2
{
    const struct
    {
        MWIN_INSPECTABLE
        void* haptics;
        void* nonRoamableId;
        HRESULT(STDMETHODCALLTYPE* name)(RawController2* self, void** value);
    }* v;
};

struct RawStatics
{
    const struct
    {
        MWIN_INSPECTABLE
        void* addAdded;
        void* removeAdded;
        void* addRemoved;
        void* removeRemoved;
        void* controllers;
        HRESULT(STDMETHODCALLTYPE* fromController)(RawStatics* self, Object* controller,
                                                   RawController** value);
    }* v;
};

struct BatteryInfo
{
    const struct
    {
        MWIN_INSPECTABLE
        HRESULT(STDMETHODCALLTYPE* report)(BatteryInfo* self, BatteryReport** value);
    }* v;
};

struct BatteryReport
{
    const struct
    {
        MWIN_INSPECTABLE
        void* chargeRate;
        void* designCapacity;
        HRESULT(STDMETHODCALLTYPE* full)(BatteryReport* self, IntReference** value);
        HRESULT(STDMETHODCALLTYPE* remaining)(BatteryReport* self, IntReference** value);
        HRESULT(STDMETHODCALLTYPE* status)(BatteryReport* self, INT32* value);
    }* v;
};

struct IntReference
{
    const struct
    {
        MWIN_INSPECTABLE
        HRESULT(STDMETHODCALLTYPE* value)(IntReference* self, INT32* value);
    }* v;
};

// The handler of the pads' arrivals and removals: its own references,
// as the runtime may hold it past the context, and a flag the pump
// takes.
typedef struct HandlerVtbl
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(mwinWgiHandler* self, REFIID iid, void** object);
    ULONG(STDMETHODCALLTYPE* AddRef)(mwinWgiHandler* self);
    ULONG(STDMETHODCALLTYPE* Release)(mwinWgiHandler* self);
    HRESULT(STDMETHODCALLTYPE* Invoke)(mwinWgiHandler* self, Object* sender, Gamepad* pad);
} HandlerVtbl;

struct mwinWgiHandler
{
    const HandlerVtbl* v;
    volatile LONG references;
    volatile LONG changed;
};

// The interfaces' ids, as the runtime's metadata gives them.
static const IID s_iidUnknown = {
    0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID s_iidAgile = {
    0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
static const IID s_iidHandler = {
    0x8a7639ee, 0x624a, 0x501a, {0xbb, 0x53, 0x56, 0x2d, 0x1e, 0xc1, 0x1b, 0x52}};
static const IID s_iidGamepadStatics = {
    0x8bbce529, 0xd49c, 0x39e9, {0x95, 0x60, 0xe4, 0x7d, 0xde, 0x96, 0xb7, 0xc8}};
static const IID s_iidController = {
    0x1baf6522, 0x5f64, 0x42c5, {0x82, 0x67, 0xb9, 0xfe, 0x22, 0x15, 0xbf, 0xbd}};
static const IID s_iidRawStatics = {
    0xeb8d0792, 0xe95a, 0x4b19, {0xaf, 0xc7, 0x0a, 0x59, 0xf8, 0xbf, 0x75, 0x9e}};
static const IID s_iidRawController2 = {
    0x43c0c035, 0xbb73, 0x4756, {0xa7, 0x87, 0x3e, 0xd6, 0xbe, 0xa6, 0x17, 0xbd}};
static const IID s_iidBatteryInfo = {
    0xdcecc681, 0x3963, 0x4da6, {0x95, 0x5d, 0x55, 0x3f, 0x3b, 0x6f, 0x61, 0x61}};

static bool Same(REFIID a, const IID* b)
{
    return memcmp(a, b, sizeof(IID)) == 0;
}

static HRESULT STDMETHODCALLTYPE HandlerQuery(mwinWgiHandler* self, REFIID iid, void** object)
{
    // Agile: the runtime may call it on any thread, which the flag allows.
    if (Same(iid, &s_iidUnknown) || Same(iid, &s_iidAgile) || Same(iid, &s_iidHandler))
    {
        *object = self;
        (void)InterlockedIncrement(&self->references);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE HandlerAddRef(mwinWgiHandler* self)
{
    return (ULONG)InterlockedIncrement(&self->references);
}

static ULONG STDMETHODCALLTYPE HandlerRelease(mwinWgiHandler* self)
{
    LONG left = InterlockedDecrement(&self->references);
    if (left == 0)
    {
        (void)HeapFree(GetProcessHeap(), 0, self);
    }
    return (ULONG)left;
}

static HRESULT STDMETHODCALLTYPE HandlerInvoke(mwinWgiHandler* self, Object* sender, Gamepad* pad)
{
    (void)sender;
    (void)pad;
    (void)InterlockedExchange(&self->changed, 1);
    return S_OK;
}

static const HandlerVtbl s_handler = {HandlerQuery, HandlerAddRef, HandlerRelease, HandlerInvoke};

// A function of combase.dll, its bytes copied: a FARPROC is no function
// of the right type to cast.
static bool Load(HMODULE library, const char* name, void* function, size_t size)
{
    FARPROC found = GetProcAddress(library, name);
    memcpy(function, (const void*)&found, size);
    return found != nullptr;
}

// A runtime class's statics, through the interface iid.
static void* Activate(const mwinWgi* wgi, const WCHAR* name, const IID* iid)
{
    void* string = nullptr;
    void* statics = nullptr;
    if (SUCCEEDED(wgi->makeString(name, (UINT32)wcslen(name), &string)))
    {
        if (FAILED(wgi->activate(string, iid, &statics)))
        {
            statics = nullptr;
        }
        (void)wgi->deleteString(string);
    }
    return statics;
}

static int32_t List(void* self, void** pads, uint32_t capacity)
{
    GamepadStatics* statics = ((mwinWgi*)self)->gamepads;
    GamepadList* list = nullptr;
    UINT32 size = 0;
    if (FAILED(statics->v->gamepads(statics, &list)))
    {
        return -1;
    }
    if (FAILED(list->v->size(list, &size)))
    {
        size = 0;
    }
    uint32_t count = 0;
    for (UINT32 i = 0; i < size && count < capacity; i++)
    {
        Gamepad* pad = nullptr;
        if (SUCCEEDED(list->v->at(list, i, &pad)) && pad != nullptr)
        {
            pads[count++] = pad;
        }
    }
    (void)list->v->Release(list);
    return (int32_t)count;
}

static void Release(void* self, void* pad)
{
    (void)self;
    Gamepad* gamepad = pad;
    (void)gamepad->v->Release(gamepad);
}

// A reading in the contract's terms: View is select and Menu start;
// sticks' y turned so down is positive.
static bool Read(void* self, void* pad, mwinPadReading* reading)
{
    (void)self;
    Gamepad* gamepad = pad;
    Reading value = {0};
    if (FAILED(gamepad->v->read(gamepad, &value)))
    {
        return false;
    }
    *reading = (mwinPadReading){.timestamp = value.timestamp};
    for (uint32_t i = 0; i < MWIN_GAMEPAD_BUTTONS; i++)
    {
        reading->buttons |= ((uint32_t)value.buttons & s_buttons[i]) != 0 ? 1u << i : 0;
    }
    reading->axes[mwin_padStickLeftX] = (float)value.leftX;
    reading->axes[mwin_padStickLeftY] = (float)-value.leftY;
    reading->axes[mwin_padStickRightX] = (float)value.rightX;
    reading->axes[mwin_padStickRightY] = (float)-value.rightY;
    reading->axes[mwin_padTriggerLeft] = (float)value.leftTrigger;
    reading->axes[mwin_padTriggerRight] = (float)value.rightTrigger;
    return true;
}

// The grips' motors and the triggers'.
static bool Vibrate(void* self, void* pad, const float motors[4])
{
    (void)self;
    Gamepad* gamepad = pad;
    Vibration value = {(double)motors[0], (double)motors[1], (double)motors[2], (double)motors[3]};
    return SUCCEEDED(gamepad->v->setVibration(gamepad, value));
}

// Whether a pad has motors in its triggers: Microsoft's from the Xbox
// One on; the Xbox 360's and other makers' pads have none, and the
// runtime would fold the triggers' strengths into the grips'.
static bool HasTriggerMotors(uint16_t vendor, uint16_t product)
{
    static const uint16_t xbox360[] = {0x028E, 0x028F, 0x0291, 0x02A0, 0x02A1, 0x0719};
    bool found = vendor == 0x045E;
    for (size_t i = 0; found && i < sizeof(xbox360) / sizeof(xbox360[0]); i++)
    {
        found = product != xbox360[i];
    }
    return found;
}

// The name the controller gives, as UTF-8 that fits: at most 21 UTF-16
// units, which make at most 63 bytes, a pair never split.
static bool NameOf(const mwinWgi* wgi, RawController* raw, mwinGamepadInfo* info)
{
    RawController2* named = nullptr;
    void* string = nullptr;
    size_t bytes = 0;
    if (FAILED(raw->v->QueryInterface(raw, &s_iidRawController2, (void**)&named)))
    {
        return false;
    }
    if (SUCCEEDED(named->v->name(named, &string)) && string != nullptr)
    {
        UINT32 length = 0;
        const WCHAR* text = wgi->stringText(string, &length);
        length = length > 21 ? 21 : length;
        if (length > 0 && IS_HIGH_SURROGATE(text[length - 1]))
        {
            length -= 1;
        }
        muniTextResult converted =
            muniConvertUtf16ToUtf8((const uint16_t*)text, length, info->name, sizeof(info->name),
                                   muni_convertReplace, &bytes);
        bytes = converted.status == muni_success ? bytes : 0;
        (void)wgi->deleteString(string);
    }
    (void)named->v->Release(named);
    info->nameLength = (uint32_t)bytes;
    return bytes > 0;
}

static void Describe(void* self, void* pad, mwinGamepadInfo* info)
{
    const mwinWgi* wgi = self;
    Gamepad* gamepad = pad;
    RawStatics* statics = wgi->controllers;
    info->capabilities = mwin_padRumble;
    Object* controller = nullptr;
    RawController* raw = nullptr;
    bool named = false;
    if (statics != nullptr &&
        SUCCEEDED(gamepad->v->QueryInterface(gamepad, &s_iidController, (void**)&controller)))
    {
        if (SUCCEEDED(statics->v->fromController(statics, controller, &raw)) && raw != nullptr)
        {
            (void)raw->v->vendor(raw, &info->vendor);
            (void)raw->v->product(raw, &info->product);
            named = NameOf(wgi, raw, info);
            (void)raw->v->Release(raw);
        }
        (void)controller->v->Release(controller);
    }
    if (!named)
    {
        static const char plain[] = "Xbox controller";
        memcpy(info->name, plain, sizeof(plain) - 1);
        info->nameLength = sizeof(plain) - 1;
    }
    if (HasTriggerMotors(info->vendor, info->product))
    {
        info->capabilities |= mwin_padTriggerRumble;
    }
}

// An int the report may leave out.
static bool IntOf(IntReference* reference, INT32* value)
{
    if (reference == nullptr)
    {
        return false;
    }
    bool read = SUCCEEDED(reference->v->value(reference, value));
    (void)reference->v->Release(reference);
    return read;
}

static int8_t Battery(void* self, void* pad)
{
    (void)self;
    Gamepad* gamepad = pad;
    BatteryInfo* info = nullptr;
    BatteryReport* report = nullptr;
    if (FAILED(gamepad->v->QueryInterface(gamepad, &s_iidBatteryInfo, (void**)&info)))
    {
        return -1;
    }
    HRESULT reported = info->v->report(info, &report);
    (void)info->v->Release(info);
    if (FAILED(reported) || report == nullptr)
    {
        return -1;
    }
    // A status of 0 is no battery: a wired pad.
    INT32 status = 0;
    IntReference* fullReference = nullptr;
    IntReference* remainingReference = nullptr;
    INT32 full = 0;
    INT32 remaining = 0;
    (void)report->v->status(report, &status);
    (void)report->v->full(report, &fullReference);
    (void)report->v->remaining(report, &remainingReference);
    bool known = IntOf(fullReference, &full) && IntOf(remainingReference, &remaining);
    (void)report->v->Release(report);
    if (status == 0 || !known || full <= 0 || remaining < 0)
    {
        return -1;
    }
    int64_t percent = (int64_t)remaining * 100 / full;
    return (int8_t)(percent > 100 ? 100 : percent);
}

static bool Changed(void* self)
{
    mwinWgiHandler* handler = ((mwinWgi*)self)->handler;
    return handler != nullptr && InterlockedExchange(&handler->changed, 0) != 0;
}

// The handler, registered for arrivals and removals; without it the
// pads are still found when they are looked for.
static void Listen(mwinWgi* wgi)
{
    GamepadStatics* statics = wgi->gamepads;
    wgi->handler = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(mwinWgiHandler));
    if (wgi->handler == nullptr)
    {
        return;
    }
    wgi->handler->v = &s_handler;
    wgi->handler->references = 1;
    Token token = {0};
    wgi->listening[0] = SUCCEEDED(statics->v->addAdded(statics, wgi->handler, &token));
    wgi->tokens[0] = token.value;
    wgi->listening[1] = SUCCEEDED(statics->v->addRemoved(statics, wgi->handler, &token));
    wgi->tokens[1] = token.value;
}

static bool LoadRuntime(mwinWgi* wgi)
{
    wgi->combase = LoadLibraryW(L"combase.dll");
    return wgi->combase != nullptr &&
           Load(wgi->combase, "RoGetActivationFactory", (void*)&wgi->activate,
                sizeof(wgi->activate)) &&
           Load(wgi->combase, "WindowsCreateString", (void*)&wgi->makeString,
                sizeof(wgi->makeString)) &&
           Load(wgi->combase, "WindowsDeleteString", (void*)&wgi->deleteString,
                sizeof(wgi->deleteString)) &&
           Load(wgi->combase, "WindowsGetStringRawBuffer", (void*)&wgi->stringText,
                sizeof(wgi->stringText));
}

bool mwinWgiStart(mwinWgi* wgi, mwinPadRuntime* runtime)
{
    *wgi = (mwinWgi){0};
    // The runtime needs COM on the thread; the drop target may have
    // started it already, and a thread in another apartment has it too.
    wgi->com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    if (LoadRuntime(wgi))
    {
        wgi->gamepads = Activate(wgi, L"Windows.Gaming.Input.Gamepad", &s_iidGamepadStatics);
    }
    if (wgi->gamepads == nullptr)
    {
        mwinWgiStop(wgi);
        return false;
    }
    wgi->controllers = Activate(wgi, L"Windows.Gaming.Input.RawGameController", &s_iidRawStatics);
    Listen(wgi);
    *runtime = (mwinPadRuntime){.self = wgi,
                                .list = List,
                                .release = Release,
                                .read = Read,
                                .vibrate = Vibrate,
                                .describe = Describe,
                                .battery = Battery,
                                .changed = Changed};
    return true;
}

void mwinWgiStop(mwinWgi* wgi)
{
    GamepadStatics* statics = wgi->gamepads;
    if (wgi->handler != nullptr)
    {
        if (wgi->listening[0])
        {
            (void)statics->v->removeAdded(statics, (Token){wgi->tokens[0]});
        }
        if (wgi->listening[1])
        {
            (void)statics->v->removeRemoved(statics, (Token){wgi->tokens[1]});
        }
        (void)HandlerRelease(wgi->handler);
    }
    if (statics != nullptr)
    {
        (void)statics->v->Release(statics);
    }
    RawStatics* controllers = wgi->controllers;
    if (controllers != nullptr)
    {
        (void)controllers->v->Release(controllers);
    }
    if (wgi->combase != nullptr)
    {
        FreeLibrary(wgi->combase);
    }
    if (wgi->com)
    {
        CoUninitialize();
    }
    *wgi = (mwinWgi){0};
}
