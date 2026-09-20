/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// This is a partial file designed to be included by other files when
// constructing various tables.

// clang-format off
XE_HID_KBM_BINDING(Guide,       "Xbox Guide"             , keybind_guide            , ""                )
XE_HID_KBM_BINDING(DpadLeft,    "D-pad Left"             , keybind_dpad_left        , "Key.Digit1"      )
XE_HID_KBM_BINDING(DpadRight,   "D-pad Right"            , keybind_dpad_right       , "Key.Digit2"      )
XE_HID_KBM_BINDING(DpadDown,    "D-pad Down"             , keybind_dpad_down        , "Key.Digit3"      )
XE_HID_KBM_BINDING(DpadUp,      "D-pad Up"               , keybind_dpad_up          , "Key.Digit4"      )
XE_HID_KBM_BINDING(LThumbLeft,  "Left Stick Left"        , keybind_left_thumb_left  , "Key.A"           )
XE_HID_KBM_BINDING(LThumbRight, "Left Stick Right"       , keybind_left_thumb_right , "Key.D"           )
XE_HID_KBM_BINDING(LThumbDown,  "Left Stick Down"        , keybind_left_thumb_down  , "Key.S"           )
XE_HID_KBM_BINDING(LThumbUp,    "Left Stick Up"          , keybind_left_thumb_up    , "Key.W"           )
XE_HID_KBM_BINDING(LThumbPress, "Left Stick Click (LS)"  , keybind_left_thumb       , "Key.C"           )
XE_HID_KBM_BINDING(RThumbUp,    "Right Stick Up"         , keybind_right_thumb_up   , "Key.ArrowUp"     )
XE_HID_KBM_BINDING(RThumbDown,  "Right Stick Down"       , keybind_right_thumb_down , "Key.ArrowDown"   )
XE_HID_KBM_BINDING(RThumbRight, "Right Stick Right"      , keybind_right_thumb_right, "Key.ArrowRight"  )
XE_HID_KBM_BINDING(RThumbLeft,  "Right Stick Left"       , keybind_right_thumb_left , "Key.ArrowLeft"   )
XE_HID_KBM_BINDING(RThumbPress, "Right Stick Click (RS)" , keybind_right_thumb      , "Mouse.Middle"    )
XE_HID_KBM_BINDING(X,           "X Button"               , keybind_x                , "Key.E"           )
XE_HID_KBM_BINDING(B,           "B Button"               , keybind_b                , "Key.F"           )
XE_HID_KBM_BINDING(A,           "A Button"               , keybind_a                , "Key.Space"       )
XE_HID_KBM_BINDING(Y,           "Y Button"               , keybind_y                , "Key.Q"           )
XE_HID_KBM_BINDING(LTrigger,    "Left Trigger (LT)"      , keybind_left_trigger     , "Mouse.Right"     )
XE_HID_KBM_BINDING(RTrigger,    "Right Trigger (RT)"     , keybind_right_trigger    , "Mouse.Left"      )
XE_HID_KBM_BINDING(Back,        "Back"                   , keybind_back             , "Key.Tab"         )
XE_HID_KBM_BINDING(Start,       "Start"                  , keybind_start            , "Key.Escape"      )
XE_HID_KBM_BINDING(LShoulder,   "Left Bumper (LB)"       , keybind_left_shoulder    , "Key.Shift"       )
XE_HID_KBM_BINDING(RShoulder,   "Right Bumper (RB)"      , keybind_right_shoulder   , "Key.R"           )
// clang-format on
