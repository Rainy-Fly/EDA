#include"./UndoStack.hpp"

namespace CanvasUndo{

UndoStack::UndoStack(std::size_t max_steps, std::size_t max_bytes)
    : max_steps_(max_steps == 0 ? 1 : max_steps),
      max_bytes_(max_bytes == 0 ? 1 : max_bytes){}

void UndoStack::clear(){
    undo_.clear();
    redo_.clear();
    undo_bytes_ = 0;
    redo_bytes_ = 0;
}

void UndoStack::clear_redo(){
    redo_.clear();
    redo_bytes_ = 0;
}

void UndoStack::push(const Snapshot& prev){
    const std::size_t bytes = estimate_bytes(prev);
    undo_.push_back(prev);
    undo_bytes_ += bytes;
    clear_redo();          //新改动让重做历史失效
    trim_undo();
}

bool UndoStack::undo(const Snapshot& current, Snapshot* restore_to){
    if(undo_.empty() || !restore_to) return false;

    redo_.push_back(current);
    redo_bytes_ += estimate_bytes(current);
    trim_redo();

    *restore_to = std::move(undo_.back());
    undo_bytes_ -= estimate_bytes(undo_.back());
    undo_.pop_back();
    return true;
}

bool UndoStack::redo(const Snapshot& current, Snapshot* restore_to){
    if(redo_.empty() || !restore_to) return false;

    undo_.push_back(current);
    undo_bytes_ += estimate_bytes(current);
    trim_undo();

    *restore_to = std::move(redo_.back());
    redo_bytes_ -= estimate_bytes(redo_.back());
    redo_.pop_back();
    return true;
}

// 粗估一份快照占用的字节数：够用来做上限控制，不需要精确。
// 主要算字符串与容器元素的净长度，再加一个固定开销估计。
std::size_t UndoStack::estimate_bytes(const Snapshot& s){
    std::size_t total = sizeof(Snapshot);

    for(const DealProjectXML::ComponentData& c : s.components){
        total += sizeof(c) + 96;
        total += c.type.size() + c.name.size() + c.category.size() + c.library_id.size()
               + c.definition_version.size() + c.reference.size() + c.value.size()
               + c.footprint.size() + c.description.size() + c.mirror.size();
        for(const auto& kv : c.params) total += kv.first.size() + kv.second.size() + 48;
        for(const DealProjectXML::PinOverrideData& p : c.pin_overrides)
            total += p.id.size() + p.number.size() + 56;
    }

    for(const DealProjectXML::WireData& w : s.wires){
        total += sizeof(w) + 96 + w.type.size() + w.name.size();
        total += w.points.size() * sizeof(wxPoint);
        for(const auto& kv : w.params) total += kv.first.size() + kv.second.size() + 48;
        total += w.start.pin_id.size() + w.start.junction_id.size() + w.start.label_id.size();
        total += w.end.pin_id.size()   + w.end.junction_id.size()   + w.end.label_id.size();
    }

    total += s.junctions.size() * (sizeof(DealProjectXML::JunctionData) + 48);
    total += s.labels.size() * (sizeof(DealProjectXML::LabelData) + 96);
    for(const DealProjectXML::LabelData& l : s.labels) total += l.text.size();
    total += s.no_connects.size() * (sizeof(DealProjectXML::NoConnectData) + 48);
    total += s.libraries.size() * (sizeof(DealProjectXML::LibraryData) + 48);
    return total;
}

void UndoStack::trim_undo(){
    while(!undo_.empty() && (undo_.size() > max_steps_ || undo_bytes_ > max_bytes_)){
        undo_bytes_ -= estimate_bytes(undo_.front());
        undo_.erase(undo_.begin());      //丢最旧的
    }
}

void UndoStack::trim_redo(){
    while(!redo_.empty() && (redo_.size() > max_steps_ || redo_bytes_ > max_bytes_)){
        redo_bytes_ -= estimate_bytes(redo_.front());
        redo_.erase(redo_.begin());
    }
}

} // namespace CanvasUndo
