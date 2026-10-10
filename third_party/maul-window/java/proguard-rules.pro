# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# What an application that shrinks or obfuscates its code (R8, ProGuard)
# keeps for Maul Window (mwin-0041). The native library finds the
# classes of maul.window by name and calls their members by name and
# signature through JNI, and finds an accessibility root's public
# virtualViewAt by reflection when the root cannot implement
# maul.window.Explorer, so none of them may be renamed or removed.
-keep class maul.window.* { *; }
-keepclassmembers class * {
    public int virtualViewAt(float, float);
}
