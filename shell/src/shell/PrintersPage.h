#pragma once

#include <string>

// Settings → Printers, and the print dialog
namespace m3::ui {
class Flex;
}

namespace m3::shell {
class Popup;
}

namespace m3::shell::panels {

void printersPage(ui::Flex* page, Popup&);
// arg: the files, one per line
void printDialog(ui::Flex* card, Popup& popup, const std::string& arg);

} // namespace m3::shell::panels
