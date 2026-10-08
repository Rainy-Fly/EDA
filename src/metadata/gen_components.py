#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 src/assets/electronic-symbols/SVG/ 下所有元件的元数据JSON。
文件名格式: <分类>-<样式>-<名称>.svg（分类在文件名前缀里）。
每个分类生成一个 src/metadata/<分类小写>.json，供资源树(Explorer)与画布加载。
用法: python3 src/metadata/gen_components.py   （在仓库根目录运行）
"""
import os, json, re, sys

SVG_DIR = os.path.join("src", "assets", "electronic-symbols", "SVG")
OUT_DIR = os.path.join("src", "metadata")
BASE_ID = 1000          # 避开已有ID(1-6, 101-102, 201-203, 301-303, 401-402)

# 15个分类的中文名与默认描述
CATEGORY = {
    "Antenna": ("天线",       "天线：发射或接收电磁波"),
    "Audio":   ("音频",       "音频器件：发声或拾音"),
    "Capacitor":("电容",     "电容：储能、滤波、耦合"),
    "Diode":   ("二极管",     "二极管：单向导通"),
    "Fuse":    ("保险丝",     "保险丝：过流保护"),
    "Ground":  ("接地",       "接地：参考电位符号"),
    "IC":      ("集成电路",   "集成电路/功能块符号"),
    "Inductor":("电感",       "电感：储能、滤波"),
    "Miscellaneous":("杂项",  "杂项器件"),
    "Relay":   ("继电器",     "继电器：电磁开关"),
    "Resistor":("电阻",       "电阻：限制电流、分压"),
    "Source":  ("电源/信号源", "电源/信号源"),
    "Switch":  ("开关",       "开关：通断控制"),
    "Transformer":("变压器",  "变压器：电压变换、隔离"),
    "Transistor":("晶体管",   "晶体管：放大、开关"),
}

# 已由 gates.json + 工具栏表示的6个逻辑门，跳过避免元数据键冲突
SKIP = {"IC-COM-Logic-AND", "IC-COM-Logic-NAND", "IC-COM-Logic-OR",
        "IC-COM-Logic-NOR", "IC-COM-Logic-XOR", "IC-COM-Logic-Inverter"}

# 去掉分类前缀后的"样式-名称"部分 -> 中文名（未列出的回退为英文转可读名）
CN = {
 # Antenna
 "COM-Aerial":"通用天线","COM-Dipole":"偶极天线","COM-Loop":"环形天线",
 # Audio
 "COM-Buzzer":"蜂鸣器","COM-Loudspeaker":"扬声器",
 "IEC-Microphone":"麦克风(IEC)","IEEE-Microphone":"麦克风(IEEE)",
 # Capacitor
 "COM-Feedthrough":"穿心电容","IEC-NonPolarized":"无极性电容(IEC)",
 "IEC-Polarized":"极性电容(IEC)","IEC-Trimmer":"微调电容(IEC)",
 "IEC-Variable":"可变电容(IEC)","IEEE-NonPolarized":"无极性电容(IEEE)",
 "IEEE-Polarized":"极性电容(IEEE)",
 # Diode
 "COM-Laser":"激光二极管","COM-LED":"发光二极管(LED)","COM-Photodiode":"光电二极管",
 "COM-Shockley":"肖克利二极管","COM-Shottky":"肖特基二极管","COM-Standard":"标准二极管",
 "COM-Tunnel":"隧道二极管","COM-Varicap":"变容二极管","COM-Zener":"齐纳二极管",
 # Fuse
 "IEC":"保险丝(IEC)","IEEE":"保险丝(IEEE)","IEEE-Alt":"保险丝(IEEE-Alt)",
 # Ground
 "COM-Chassis":"机壳地","COM-General":"一般接地","COM-Signal":"信号地",
 # IC（除6个逻辑门）
 "COM-Comparator":"比较器","COM-FlipFlop-ClockedD":"D触发器(时钟)",
 "COM-FlipFlop-ClockedJK":"JK触发器(时钟)","COM-FlipFlop-ClockedT":"T触发器(时钟)",
 "COM-FlipFlop-GatedD":"D锁存器(门控)","COM-FlipFlop-GatedSR":"SR锁存器(门控)",
 "COM-FlipFlop-SimpleSR":"SR锁存器(基本)","COM-Logic-Buffer":"缓冲器",
 "COM-Logic-XNOR":"同或门","COM-OpAmp":"运算放大器",
 "COM-Schmitt-Inverted":"反相施密特触发器","COM-Schmitt":"施密特触发器",
 # Inductor
 "COM-Air":"空心电感","COM-Ferrite-Bead":"磁珠","COM-Magnetic":"磁芯电感",
 "COM-Tapped":"抽头电感","COM-Variable":"可变电感",
 # Miscellaneous
 "COM-ADC":"模数转换器(ADC)","COM-Crystal_Oscillator":"晶振","COM-DAC":"数模转换器(DAC)",
 "COM-Lamp-Incandescent":"白炽灯","COM-Lamp-Indicator":"指示灯",
 "COM-Optocoupler":"光电耦合器","COM-Probe_Point":"测试点",
 # Relay
 "COM-COM-SPDT":"单刀双掷继电器","COM-COM-SPST-NC":"单刀单掷常闭继电器",
 "COM-COM-SPST-NO":"单刀单掷常开继电器","IEC-SPDT":"单刀双掷继电器(IEC)",
 "IEC-SPST-NC":"单刀单掷常闭继电器(IEC)","IEC-SPST-NO":"单刀单掷常开继电器(IEC)",
 "IEEE-SPDT":"单刀双掷继电器(IEEE)","IEEE-SPST-NC":"单刀单掷常闭继电器(IEEE)",
 "IEEE-SPST-NO":"单刀单掷常开继电器(IEEE)",
 # Resistor
 "COM-Memristor":"忆阻器","IEC-Photoresistor":"光敏电阻(IEC)",
 "IEC-Potentiometer":"电位器(IEC)","IEC-Rheostat":"变阻器(IEC)",
 "IEC-Standard":"标准电阻(IEC)","IEC-Thermistor":"热敏电阻(IEC)",
 "IEC-Trimmer":"微调电阻(IEC)","IEC-Varistor":"压敏电阻(IEC)",
 "IEEE-Photoresistor":"光敏电阻(IEEE)","IEEE-Potentiometer":"电位器(IEEE)",
 "IEEE-Rheostat":"变阻器(IEEE)","IEEE-Standard":"标准电阻(IEEE)",
 "IEEE-Thermistor":"热敏电阻(IEEE)","IEEE-Trimmer":"微调电阻(IEEE)",
 "IEEE-Varistor":"压敏电阻(IEEE)",
 # Source
 "COM-AC":"交流源","COM-Battery-Multiple":"多节电池","COM-Battery-Single":"单节电池",
 "COM-Current-Controlled":"受控电流源","COM-Current":"电流源",
 "COM-DC-Controlled":"受控直流源","COM-DC":"直流源","COM-Photovoltaic":"光伏电池",
 "COM-Square":"方波源","COM-Triangle":"三角波源",
 # Switch
 "COM-DPDT":"双刀双掷开关","COM-DPST":"双刀单掷开关",
 "COM-Pushbutton-NC":"常闭按钮开关","COM-Pushbutton-NO":"常开按钮开关",
 "COM-Pushbutton-Two_Circuit":"双回路按钮开关","COM-SPDT":"单刀双掷开关",
 "COM-SPST":"单刀单掷开关",
 # Transformer
 "COM-Center-Double":"双中心抽头变压器","COM-Center":"中心抽头变压器",
 "COM-Standard":"标准变压器",
 # Transistor
 "COM-BJT-NPN":"NPN双极型晶体管","COM-BJT-PNP":"PNP双极型晶体管",
 "COM-Darlington-NPN":"NPN达林顿管","COM-Darlington-PNP":"PNP达林顿管",
 "COM-JFET-N":"N沟道结型场效应管","COM-JFET-P":"P沟道结型场效应管",
 "COM-MOSFET-N-Depletion":"N沟道耗尽型MOS管","COM-MOSFET-N-Enhancement":"N沟道增强型MOS管",
 "COM-MOSFET-N":"N沟道MOS管","COM-MOSFET-P-Depletion":"P沟道耗尽型MOS管",
 "COM-MOSFET-P-Enhancement":"P沟道增强型MOS管","COM-MOSFET-P":"P沟道MOS管",
 "COM-Phototransitor":"光电晶体管",
}

def humanize(s):
    """把英文标识转成可读英文名，如 FlipFlop-ClockedD -> FlipFlop ClockedD"""
    return s.replace("_", " ").replace("-", " ")

def main():
    if not os.path.isdir(SVG_DIR):
        print("找不到SVG目录:", SVG_DIR); sys.exit(1)
    files = sorted(f for f in os.listdir(SVG_DIR) if f.endswith(".svg"))
    groups = {}   # 分类 -> [(type, 中文名, 英文名)]
    missing = []
    for f in files:
        stem = f[:-4]
        if stem in SKIP:
            continue
        parts = stem.split("-", 1)
        cat = parts[0]
        rest = parts[1] if len(parts) > 1 else ""
        if cat not in CATEGORY:
            missing.append(f); continue
        cn = CN.get(rest)
        if not cn:
            cn = humanize(rest)
            missing.append(f + "  (中文名回退: " + cn + ")")
        en = humanize(rest)
        groups.setdefault(cat, []).append((stem, cn, en))

    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)
    nid = BASE_ID
    total = 0
    for cat in sorted(groups):
        entries = []
        for type_s, cn, en in sorted(groups[cat], key=lambda t: t[0]):
            desc = CATEGORY[cat][1]
            entries.append({
                "id": nid, "name": cn, "name_en": type_s,
                "type": type_s, "category": cat, "description": desc,
            })
            nid += 1; total += 1
        doc = {
            "file": cat.lower() + ".json",
            "description": "电子符号库分类：" + CATEGORY[cat][0] + "（" + cat + "）",
            "components": entries,
        }
        path = os.path.join(OUT_DIR, cat.lower() + ".json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(doc, f, ensure_ascii=False, indent=2)
            f.write("\n")
        print("生成 %-14s %3d 个元件 -> %s" % (cat, len(entries), path))
    print("共生成 %d 个元件，ID范围 %d-%d" % (total, BASE_ID, nid - 1))
    if missing:
        print("警告：以下文件未生成元数据：")
        for m in missing: print("  " + m)

if __name__ == "__main__":
    main()
