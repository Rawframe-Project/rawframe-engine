# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# What an application that shrinks or obfuscates its code (R8, ProGuard)
# keeps for Maul UI (record mui-0008). The native adapter loads
# maul.ui.AccessProvider by name, makes it and reads and calls its
# members by name and signature through JNI, binding its native methods
# by name, and Maul Window finds its virtualViewAt by reflection, so
# none of them may be renamed or removed.
-keep class maul.ui.* { *; }
