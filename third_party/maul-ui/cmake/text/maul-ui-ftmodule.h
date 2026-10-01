// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The FreeType modules Maul UI builds (record mui-0006): TrueType and CFF
// outlines, the PostScript hinter CFF uses, the sfnt tables, and the
// smooth rasterizer.
FT_USE_MODULE(FT_Driver_ClassRec, tt_driver_class)
FT_USE_MODULE(FT_Driver_ClassRec, cff_driver_class)
FT_USE_MODULE(FT_Module_Class, psaux_module_class)
FT_USE_MODULE(FT_Module_Class, psnames_module_class)
FT_USE_MODULE(FT_Module_Class, pshinter_module_class)
FT_USE_MODULE(FT_Module_Class, sfnt_module_class)
FT_USE_MODULE(FT_Renderer_Class, ft_smooth_renderer_class)
