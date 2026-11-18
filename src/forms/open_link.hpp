#pragma once
#include <string>
namespace paint::forms {
// Opens a web address in the system browser: ShellExecuteW on Windows,
// /usr/bin/open on macOS, xdg-open elsewhere. GUI.Forms has no such service
// yet; only Plan Paint's own fixed addresses (the business card's) come here,
// never text from a picture or a file. Failure leaves nothing behind and is
// not reported: the browser simply does not open.
void open_link(const std::string& address);
} // namespace paint::forms
