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
XE_HID_KBM_BINDING(DpadLeft,    "D-pad Left"             , keybind_dpad_left        , "1"               )
XE_HID_KBM_BINDING(DpadRight,   "D-pad Right"            , keybind_dpad_right       , "2"               )
XE_HID_KBM_BINDING(DpadDown,    "D-pad Down"             , keybind_dpad_down        , "3"               )
XE_HID_KBM_BINDING(DpadUp,      "D-pad Up"               , keybind_dpad_up          , "4"               )
XE_HID_KBM_BINDING(LThumbLeft,  "Left Stick Left"        , keybind_left_thumb_left  , "A"               )
XE_HID_KBM_BINDING(LThumbRight, "Left Stick Right"       , keybind_left_thumb_right , "D"               )
XE_HID_KBM_BINDING(LThumbDown,  "Left Stick Down"        , keybind_left_thumb_down  , "S"               )
XE_HID_KBM_BINDING(LThumbUp,    "Left Stick Up"          , keybind_left_thumb_up    , "W"               )
XE_HID_KBM_BINDING(LThumbPress, "Left Stick Click (LS)"  , keybind_left_thumb       , "C"               )
XE_HID_KBM_BINDING(RThumbUp,    "Right Stick Up"         , keybind_right_thumb_up   , "Up"              )
XE_HID_KBM_BINDING(RThumbDown,  "Right Stick Down"       , keybind_right_thumb_down , "Down"            )
XE_HID_KBM_BINDING(RThumbRight, "Right Stick Right"      , keybind_right_thumb_right, "Right"           )
XE_HID_KBM_BINDING(RThumbLeft,  "Right Stick Left"       , keybind_right_thumb_left , "Left"            )
XE_HID_KBM_BINDING(RThumbPress, "Right Stick Click (RS)" , keybind_right_thumb      , "MouseMiddle"     )
XE_HID_KBM_BINDING(X,           "X Button"               , keybind_x                , "E"               )
XE_HID_KBM_BINDING(B,           "B Button"               , keybind_b                , "F"               )
XE_HID_KBM_BINDING(A,           "A Button"               , keybind_a                , "Space"           )
XE_HID_KBM_BINDING(Y,           "Y Button"               , keybind_y                , "Q"               )
XE_HID_KBM_BINDING(LTrigger,    "Left Trigger (LT)"      , keybind_left_trigger     , "MouseRight"      )
XE_HID_KBM_BINDING(RTrigger,    "Right Trigger (RT)"     , keybind_right_trigger    , "MouseLeft"       )
XE_HID_KBM_BINDING(Back,        "Back"                   , keybind_back             , "Tab"             )
XE_HID_KBM_BINDING(Start,       "Start"                  , keybind_start            , "Esc"             )
XE_HID_KBM_BINDING(LShoulder,   "Left Bumper (LB)"       , keybind_left_shoulder    , "Shift"           )
XE_HID_KBM_BINDING(RShoulder,   "Right Bumper (RB)"      , keybind_right_shoulder   , "R"               )
// clang-format on
