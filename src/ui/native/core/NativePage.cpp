#include "ui/native/core/NativePage.h"

namespace tools3000::ui::native {

NativePage::NativePage() = default;
NativePage::~NativePage() = default;

void NativePage::refreshState() {
    // 默认空实现，具体子类按需重载
}

} // namespace tools3000::ui::native
