#pragma once
#include<cstddef>
#include<vector>
#include"../DealProjectXML/DealProjectXML.hpp"

// 画布撤销栈（src/Canva/UndoStack）
//
// 存的是“整体快照”（DealProjectXML::ProjectData），不是逐条逆操作：
// DealProjectXML 的 CollectFromCanvas / ApplyToCanvas 已经能完整采集与还原画布
// （元件含旋转/镜像/位号/值/封装/参数/引脚覆盖，导线含折点与电气端点，
// 还有 junction / label / no-connect），所以用快照**不会漏掉任何一种操作**，
// 代价只是内存和一次重建 —— 几十个元件的电路是毫秒级、每份快照十几 KB。
//
// 容量双限：
//   - 条数上限（默认 50）
//   - 估算总字节上限（默认 32MB）—— 只限条数时，巨型画布仍可能吃光内存，
//     超了就丢最旧的
//
// 本类只做数据结构，不碰 GUI/画布，可以在控制台里单独自检。
namespace CanvasUndo{

using Snapshot = DealProjectXML::ProjectData;

class UndoStack{
public:
    explicit UndoStack(std::size_t max_steps = 50,
                       std::size_t max_bytes = 32u * 1024u * 1024u);

    void clear();

    // 记录一步：prev 是“这次改动之前”的状态。
    // 不能直接存“改动之后”，因为改动通知是改完才触发的（见 Canvas::commit_undo_step）。
    void push(const Snapshot& prev);

    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    std::size_t undo_depth() const { return undo_.size(); }
    std::size_t redo_depth() const { return redo_.size(); }

    // 撤销：current 是当前状态（会被放进 redo 栈以便重做）；
    // 成功时 restore_to 得到要恢复的状态，返回 true。
    bool undo(const Snapshot& current, Snapshot* restore_to);
    // 重做：对称操作。
    bool redo(const Snapshot& current, Snapshot* restore_to);
    // 有新的改动进来时清空 redo（历史分叉了）。
    void clear_redo();

private:
    static std::size_t estimate_bytes(const Snapshot& snapshot);
    void trim_undo();
    void trim_redo();

    std::size_t max_steps_;
    std::size_t max_bytes_;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    std::size_t undo_bytes_ = 0;
    std::size_t redo_bytes_ = 0;
};

} // namespace CanvasUndo
