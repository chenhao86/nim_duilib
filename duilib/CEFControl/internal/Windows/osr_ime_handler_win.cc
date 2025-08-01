// Copyright 2016 The Chromium Embedded Framework Authors. Portions copyright
// 2013 The Chromium Authors. All rights reserved. Use of this source code is
// governed by a BSD-style license that can be found in the LICENSE file.

// Implementation based on ui/base/ime/win/imm32_manager.cc from Chromium.

#include "osr_ime_handler_win.h"

#include <msctf.h>
#include <windowsx.h>

#include "include/base/cef_build.h"
#include "include/cef_version.h"
#include "util_win.h"

#define ColorUNDERLINE \
  0xFF000000  // Black SkColor value for underline,
              // same as Blink.
#define ColorBKCOLOR \
  0x00000000  // White SkColor value for background,
              // same as Blink.

namespace client {

namespace {

// Determines whether or not the given attribute represents a selection
bool IsSelectionAttribute(char attribute) {
  return (attribute == ATTR_TARGET_CONVERTED ||
          attribute == ATTR_TARGET_NOTCONVERTED);
}

// Helper function for OsrImeHandlerWin::GetCompositionInfo() method,
// to get the target range that's selected by the user in the current
// composition string.
void GetCompositionSelectionRange(HIMC imc,
                                  uint32_t* target_start,
                                  uint32_t* target_end) {
  uint32_t attribute_size =
      ::ImmGetCompositionString(imc, GCS_COMPATTR, nullptr, 0);
  if (attribute_size > 0) {
    uint32_t start = 0;
    uint32_t end = 0;
    std::vector<char> attribute_data(attribute_size);

    ::ImmGetCompositionString(imc, GCS_COMPATTR, &attribute_data[0],
                              attribute_size);
    for (start = 0; start < attribute_size; ++start) {
      if (IsSelectionAttribute(attribute_data[start])) {
        break;
      }
    }
    for (end = start; end < attribute_size; ++end) {
      if (!IsSelectionAttribute(attribute_data[end])) {
        break;
      }
    }

    *target_start = start;
    *target_end = end;
  }
}

// Helper function for OsrImeHandlerWin::GetCompositionInfo() method, to get
// underlines information of the current composition string.
void GetCompositionUnderlines(
    HIMC imc,
    uint32_t target_start,
    uint32_t target_end,
    std::vector<CefCompositionUnderline>& underlines) {
  int clause_size = ::ImmGetCompositionString(imc, GCS_COMPCLAUSE, nullptr, 0);
  int clause_length = clause_size / sizeof(uint32_t);
  if (clause_length) {
    std::vector<uint32_t> clause_data(clause_length);

    ::ImmGetCompositionString(imc, GCS_COMPCLAUSE, &clause_data[0],
                              clause_size);
    for (int i = 0; i < clause_length - 1; ++i) {
      cef_composition_underline_t underline = {};
      underline.range.from = clause_data[i];
      underline.range.to = clause_data[i + 1];
      underline.color = ColorUNDERLINE;
      underline.background_color = ColorBKCOLOR;
      underline.thick = 0;

      // Use thick underline for the target clause.
#if CEF_VERSION_MAJOR <= 109
      if (underline.range.from >= (int)target_start &&
          underline.range.to <= (int)target_end) {
#else
      if (underline.range.from >= target_start &&
          underline.range.to <= target_end) {
#endif
        underline.thick = 1;
      }
      underlines.push_back(underline);
    }
  }
}

}  // namespace

OsrImeHandlerWin::OsrImeHandlerWin(HWND hwnd)
    : input_language_id_(LANG_USER_DEFAULT),

      cursor_index_(std::numeric_limits<uint32_t>::max()),
      hwnd_(hwnd) {
  ime_rect_ = {-1, -1, 0, 0};
}

OsrImeHandlerWin::~OsrImeHandlerWin() {
  DestroyImeWindow();
}

void OsrImeHandlerWin::SetInputLanguage() {
  // Retrieve the current input language from the system's keyboard layout.
  // Using GetKeyboardLayoutName instead of GetKeyboardLayout, because
  // the language from GetKeyboardLayout is the language under where the
  // keyboard layout is installed. And the language from GetKeyboardLayoutName
  // indicates the language of the keyboard layout itself.
  // See crbug.com/344834.
  WCHAR keyboard_layout[KL_NAMELENGTH] = {0, };
  if (::GetKeyboardLayoutNameW(keyboard_layout)) {
    input_language_id_ =
        static_cast<LANGID>(::wcstoull(&keyboard_layout[KL_NAMELENGTH >> 1], nullptr, 16)); //字符串是16进制的
  } else {
    input_language_id_ = 0x0409;  // Fallback to en-US.
  }
}

void OsrImeHandlerWin::CreateImeWindow() {
  // Chinese/Japanese IMEs somehow ignore function calls to
  // ::ImmSetCandidateWindow(), and use the position of the current system
  // caret instead -::GetCaretPos().
  // Therefore, we create a temporary system caret for Chinese IMEs and use
  // it during this input context.
  // Since some third-party Japanese IME also uses ::GetCaretPos() to determine
  // their window position, we also create a caret for Japanese IMEs.
  if (PRIMARYLANGID(input_language_id_) == LANG_CHINESE ||
      PRIMARYLANGID(input_language_id_) == LANG_JAPANESE) {
    if (!system_caret_) {
      if (::CreateCaret(hwnd_, nullptr, 1, 1)) {
        system_caret_ = true;
      }
    }
  }
}

void OsrImeHandlerWin::DestroyImeWindow() {
  // Destroy the system caret if we have created for this IME input context.
  if (system_caret_) {
    ::DestroyCaret();
    system_caret_ = false;
  }
}

void OsrImeHandlerWin::MoveImeWindow() {
  // Does nothing when the target window has no input focus.
  if (GetFocus() != hwnd_) {
    return;
  }

  CefRect rc = ime_rect_;
  uint32_t location = cursor_index_;

  // If location is not specified fall back to the composition range start.
  if (location == std::numeric_limits<uint32_t>::max()) {
    location = composition_range_.from;
  }

  // Offset location by the composition range start if required.
#if CEF_VERSION_MAJOR <= 109
  if ((int)location >= composition_range_.from) {
    location -= composition_range_.from;
  }
#else
  if (location >= composition_range_.from) {
      location -= composition_range_.from;
  }
#endif

  if (location < composition_bounds_.size()) {
    rc = composition_bounds_[location];
  } else {
    return;
  }

  HIMC imc = ::ImmGetContext(hwnd_);
  if (imc) {
    const int kCaretMargin = 1;
    if (PRIMARYLANGID(input_language_id_) == LANG_CHINESE) {
      // Chinese IMEs ignore function calls to ::ImmSetCandidateWindow()
      // when a user disables TSF (Text Service Framework) and CUAS (Cicero
      // Unaware Application Support).
      // On the other hand, when a user enables TSF and CUAS, Chinese IMEs
      // ignore the position of the current system caret and use the
      // parameters given to ::ImmSetCandidateWindow() with its 'dwStyle'
      // parameter CFS_CANDIDATEPOS.
      // Therefore, we do not only call ::ImmSetCandidateWindow() but also
      // set the positions of the temporary system caret if it exists.
      CANDIDATEFORM candidate_position = {
          0, CFS_CANDIDATEPOS, {rc.x, rc.y}, {0, 0, 0, 0}};
      ::ImmSetCandidateWindow(imc, &candidate_position);
    }
    if (system_caret_) {
      switch (PRIMARYLANGID(input_language_id_)) {
        case LANG_JAPANESE:
          ::SetCaretPos(rc.x, rc.y + rc.height);
          break;
        default:
          ::SetCaretPos(rc.x, rc.y);
          break;
      }
    }

    if (PRIMARYLANGID(input_language_id_) == LANG_KOREAN) {
      // Korean IMEs require the lower-left corner of the caret to move their
      // candidate windows.
      rc.y += kCaretMargin;
    }

    // Japanese IMEs and Korean IMEs also use the rectangle given to
    // ::ImmSetCandidateWindow() with its 'dwStyle' parameter CFS_EXCLUDE
    // Therefore, we also set this parameter here.
    CANDIDATEFORM exclude_rectangle = {
        0,
        CFS_EXCLUDE,
        {rc.x, rc.y},
        {rc.x, rc.y, rc.x + rc.width, rc.y + rc.height}};
    ::ImmSetCandidateWindow(imc, &exclude_rectangle);

    ::ImmReleaseContext(hwnd_, imc);
  }
}

void OsrImeHandlerWin::CleanupComposition() {
  // Notify the IMM attached to the given window to complete the ongoing
  // composition (when given window is de-activated while composing and
  // re-activated) and reset the composition status.
  if (is_composing_) {
    HIMC imc = ::ImmGetContext(hwnd_);
    if (imc) {
      ::ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
      ::ImmReleaseContext(hwnd_, imc);
    }
    ResetComposition();
  }
}

void OsrImeHandlerWin::ResetComposition() {
  // Reset the composition status.
  is_composing_ = false;
  cursor_index_ = std::numeric_limits<uint32_t>::max();
}

void OsrImeHandlerWin::GetCompositionInfo(
    HIMC imc,
    LPARAM lparam,
    CefString& composition_text,
    std::vector<CefCompositionUnderline>& underlines,
    int& composition_start) {
  // We only care about GCS_COMPATTR, GCS_COMPCLAUSE and GCS_CURSORPOS, and
  // convert them into underlines and selection range respectively.
  underlines.clear();

  uint32_t length = static_cast<uint32_t>(composition_text.length());

  // Find out the range selected by the user.
  uint32_t target_start = length;
  uint32_t target_end = length;
  if (lparam & GCS_COMPATTR) {
    GetCompositionSelectionRange(imc, &target_start, &target_end);
  }

  // Retrieve the selection range information. If CS_NOMOVECARET is specified
  // it means the cursor should not be moved and we therefore place the caret at
  // the beginning of the composition string. Otherwise we should honour the
  // GCS_CURSORPOS value if it's available.
  // TODO(suzhe): Due to a bug in WebKit we currently can't use selection range
  // with composition string.
  // See: https://bugs.webkit.org/show_bug.cgi?id=40805
  if (!(lparam & CS_NOMOVECARET) && (lparam & GCS_CURSORPOS)) {
    // IMM32 does not support non-zero-width selection in a composition. So
    // always use the caret position as selection range.
    int cursor = ::ImmGetCompositionString(imc, GCS_CURSORPOS, nullptr, 0);
    composition_start = cursor;
  } else {
    composition_start = 0;
  }

  // Retrieve the clause segmentations and convert them to underlines.
  if (lparam & GCS_COMPCLAUSE) {
    GetCompositionUnderlines(imc, target_start, target_end, underlines);
  }

  // Set default underlines in case there is no clause information.
  if (!underlines.size()) {
    CefCompositionUnderline underline;
    underline.color = ColorUNDERLINE;
    underline.background_color = ColorBKCOLOR;
    if (target_start > 0) {
      underline.range.from = 0;
      underline.range.to = target_start;
      underline.thick = 0;
      underlines.push_back(underline);
    }
    if (target_end > target_start) {
      underline.range.from = target_start;
      underline.range.to = target_end;
      underline.thick = 1;
      underlines.push_back(underline);
    }
    if (target_end < length) {
      underline.range.from = target_end;
      underline.range.to = length;
      underline.thick = 0;
      underlines.push_back(underline);
    }
  }
}

bool OsrImeHandlerWin::GetString(HIMC imc,
                                 WPARAM lparam,
                                 int type,
                                 CefString& result) {
  if (!(lparam & type)) {
    return false;
  }
  LONG string_size = ::ImmGetCompositionString(imc, type, nullptr, 0);
  if (string_size <= 0) {
    return false;
  }

  // For trailing nullptr - ImmGetCompositionString excludes that.
  string_size += sizeof(WCHAR);

  std::vector<wchar_t> buffer(string_size);
  ::ImmGetCompositionString(imc, type, &buffer[0], string_size);
  result.FromWString(&buffer[0]);
  return true;
}

bool OsrImeHandlerWin::GetResult(LPARAM lparam, CefString& result) {
  bool ret = false;
  HIMC imc = ::ImmGetContext(hwnd_);
  if (imc) {
    ret = GetString(imc, lparam, GCS_RESULTSTR, result);
    ::ImmReleaseContext(hwnd_, imc);
  }
  return ret;
}

bool OsrImeHandlerWin::GetComposition(
    LPARAM lparam,
    CefString& composition_text,
    std::vector<CefCompositionUnderline>& underlines,
    int& composition_start) {
  bool ret = false;
  HIMC imc = ::ImmGetContext(hwnd_);
  if (imc) {
    // Copy the composition string to the CompositionText object.
    ret = GetString(imc, lparam, GCS_COMPSTR, composition_text);

    if (ret) {
      // Retrieve the composition underlines and selection range information.
      GetCompositionInfo(imc, lparam, composition_text, underlines,
                         composition_start);

      // Mark that there is an ongoing composition.
      is_composing_ = true;
    }

    ::ImmReleaseContext(hwnd_, imc);
  }
  return ret;
}

void OsrImeHandlerWin::DisableIME() {
  CleanupComposition();
  ::ImmAssociateContextEx(hwnd_, nullptr, 0);
}

void OsrImeHandlerWin::CancelIME() {
  if (is_composing_) {
    HIMC imc = ::ImmGetContext(hwnd_);
    if (imc) {
      ::ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
      ::ImmReleaseContext(hwnd_, imc);
    }
    ResetComposition();
  }
}

void OsrImeHandlerWin::EnableIME() {
  // Load the default IME context.
  ::ImmAssociateContextEx(hwnd_, nullptr, IACE_DEFAULT);
}

void OsrImeHandlerWin::UpdateCaretPosition(uint32_t index) {
  // Save the caret position.
  cursor_index_ = index;
  // Move the IME window.
  MoveImeWindow();
}

void OsrImeHandlerWin::ChangeCompositionRange(
    const CefRange& selection_range,
    const std::vector<CefRect>& bounds) {
  composition_range_ = selection_range;
  composition_bounds_ = bounds;
  MoveImeWindow();
}

}  // namespace client
