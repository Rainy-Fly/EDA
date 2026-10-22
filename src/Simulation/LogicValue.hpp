#pragma once
#include<cstddef>
#include<string>
#include<vector>

// 三值逻辑：0 / 1 / X（未知或尚不能确定）。
// 本阶段不做高阻 Z、驱动强度和多驱动解析（见《仿真任务规划.md》3.1 节），
// 也不做四态/九态逻辑系统。
// 逻辑门必须正确处理未知值：0 AND X = 0，1 AND X = X，1 OR X = 1，0 OR X = X。
namespace Simulation{

enum class LogicValue{
    Zero,    //逻辑低
    One,     //逻辑高
    Unknown  //未知/尚未确定
};

//bool -> 逻辑值（方便测试/调用方设置输入开关）
inline LogicValue from_bool(bool value){
    return value ? LogicValue::One : LogicValue::Zero;
}

//是否已知（0 或 1）
inline bool is_known(LogicValue value){
    return value != LogicValue::Unknown;
}

//"0" / "1" / "X"（诊断信息和测试里用）
inline const char* to_string(LogicValue value){
    switch(value){
        case LogicValue::Zero: return "0";
        case LogicValue::One:  return "1";
        case LogicValue::Unknown: break;
    }
    return "X";
}

//NOT：0->1，1->0，X->X
inline LogicValue logical_not(LogicValue a){
    switch(a){
        case LogicValue::Zero: return LogicValue::One;
        case LogicValue::One:  return LogicValue::Zero;
        case LogicValue::Unknown: break;
    }
    return LogicValue::Unknown;
}

//AND：任一输入为0 -> 0；全为1 -> 1；其余 -> X
//（0 AND X = 0、1 AND X = X）
inline LogicValue logical_and(LogicValue a, LogicValue b){
    if(a == LogicValue::Zero || b == LogicValue::Zero) return LogicValue::Zero;
    if(a == LogicValue::One  && b == LogicValue::One)  return LogicValue::One;
    return LogicValue::Unknown;
}

//OR：任一输入为1 -> 1；全为0 -> 0；其余 -> X
//（1 OR X = 1、0 OR X = X）
inline LogicValue logical_or(LogicValue a, LogicValue b){
    if(a == LogicValue::One || b == LogicValue::One) return LogicValue::One;
    if(a == LogicValue::Zero && b == LogicValue::Zero) return LogicValue::Zero;
    return LogicValue::Unknown;
}

//XOR：任一输入未知 -> X；否则按异或
inline LogicValue logical_xor(LogicValue a, LogicValue b){
    if(!is_known(a) || !is_known(b)) return LogicValue::Unknown;
    return (a == b) ? LogicValue::Zero : LogicValue::One;
}

//多输入的 AND/OR/XOR：空输入按 Unknown 处理
//（调用方应先过 SimulationValidator 检查引脚数，这里只保证不越界/不崩）
inline LogicValue logical_and(const std::vector<LogicValue>& inputs){
    if(inputs.empty()) return LogicValue::Unknown;
    LogicValue result = LogicValue::One;
    for(LogicValue v : inputs) result = logical_and(result, v);
    return result;
}

inline LogicValue logical_or(const std::vector<LogicValue>& inputs){
    if(inputs.empty()) return LogicValue::Unknown;
    LogicValue result = LogicValue::Zero;
    for(LogicValue v : inputs) result = logical_or(result, v);
    return result;
}

inline LogicValue logical_xor(const std::vector<LogicValue>& inputs){
    if(inputs.empty()) return LogicValue::Unknown;
    LogicValue result = LogicValue::Zero;
    for(LogicValue v : inputs) result = logical_xor(result, v);
    return result;
}

} // namespace Simulation
