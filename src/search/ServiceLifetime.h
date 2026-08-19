#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ServiceLifetime — 索引服务的归属判定
//
// 索引服务是一个独立进程，可能由三种角色拥有：主程序自己拉起的子进程、用户
// 显式安装的 Windows 服务、或者另一个 EasyTools 实例。主程序退出时只有第一种
// 该由它负责收尾，因此把判定条件收敛成一个纯函数，便于单独验证。
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_SEARCH_SERVICELIFETIME_H
#define EASYTOOLS_SEARCH_SERVICELIFETIME_H

namespace easy::search {

/// 主程序退出时判断索引服务去留所需的全部事实。
struct ServiceOwnership {
    /// 本进程是否亲手把服务进程拉起来过。
    bool spawnedByUs = false;
    /// 服务是否由 SCM 作为 Windows 服务托管。
    bool managedByScm = false;
    /// 用户是否在设置里选择了让索引常驻。
    bool keepRunningPref = false;
};

/// 是否应当在主程序退出时一并停掉索引服务。
///
/// 只停自己拉起的那一个：SCM 托管的服务是用户显式安装的，主程序无权处置；
/// 没被自己拉起过则说明它属于别的实例，可能仍有客户端在用。
inline bool shouldStopServiceOnExit(const ServiceOwnership& ownership) noexcept {
    return ownership.spawnedByUs && !ownership.managedByScm && !ownership.keepRunningPref;
}

}  // namespace easy::search

#endif  // EASYTOOLS_SEARCH_SERVICELIFETIME_H
