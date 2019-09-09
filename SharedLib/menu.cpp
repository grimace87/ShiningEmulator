#include "menu.h"

Menu::Menu(const wchar_t* text, Action action) : text{ text }, action{ action } { }

Menu::Menu(const wchar_t* text, std::initializer_list<Menu> subMenus) :
        text{ text },
        action{ Action::MSG_UNUSED },
        subMenus { subMenus } { }

Menu Menu::buildMain() {
    // Build main menu
    return Menu {
            L"", {
                    Menu {L"Actions", {
                        Menu {L"Open ROM", Action::MSG_OPEN_FILE},
                        Menu {L"Show debug window", Action::MSG_OPEN_DEBUGGER}
                    }},
                    Menu {L"Exit", Action::MSG_EXIT}
            }
    };
}
