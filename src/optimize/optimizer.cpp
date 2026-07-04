// ToyC Optimizer 实现
// 包含多个优化 Pass：常量折叠、代数化简、拷贝传播、死代码消除、循环不变式外提

#include "toyc/optimize/optimizer.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace toyc {

namespace {

// ============================================================
// 辅助函数
// ============================================================

uint32_t regId(const IROperand& operand) {
    return std::get<uint32_t>(operand.value);
}

int32_t immValue(const IROperand& operand) {
    return std::get<int32_t>(operand.value);
}

bool isReg(const IROperand& op) {
    return op.kind == OperandKind::VirtualReg;
}

bool isImm(const IROperand& op) {
    return op.kind == OperandKind::Immediate;
}

// 获取指令中写入的目标虚拟寄存器（如果存在）
std::optional<uint32_t> destReg(const IRInstruction& inst) {
    if (inst.operands.empty()) return std::nullopt;
    if (inst.operands[0].kind == OperandKind::VirtualReg) {
        return regId(inst.operands[0]);
    }
    return std::nullopt;
}

// 获取指令中读取的虚拟寄存器列表
std::vector<uint32_t> srcRegs(const IRInstruction& inst) {
    std::vector<uint32_t> regs;
    for (size_t i = 1; i < inst.operands.size(); ++i) {
        if (inst.operands[i].kind == OperandKind::VirtualReg) {
            regs.push_back(regId(inst.operands[i]));
        }
    }
    return regs;
}

// 指令是否有副作用（不能安全删除）
bool hasSideEffect(const IRInstruction& inst) {
    switch (inst.opcode) {
        case IROpcode::STORE_GLOBAL:
        case IROpcode::STORE_LOCAL:
        case IROpcode::CALL:
        case IROpcode::RET:
        case IROpcode::JMP:
        case IROpcode::BEQ:
        case IROpcode::BNE:
        case IROpcode::PARAM:
        case IROpcode::LABEL:
            return true;
        default:
            return false;
    }
}

// ============================================================
// 代数化简规则
// ============================================================

// 尝试代数化简二元指令
// 返回化简后的新操作码和操作数（如果化简成功）
struct SimplifiedInst {
    IROpcode opcode;
    std::vector<IROperand> operands;
};

std::optional<SimplifiedInst> simplifyAlgebraic(const IRInstruction& inst) {
    if (inst.operands.size() != 3) return std::nullopt;
    const auto& rd = inst.operands[0];
    const auto& rs1 = inst.operands[1];
    const auto& rs2 = inst.operands[2];

    bool rs1Imm = isImm(rs1);
    bool rs2Imm = isImm(rs2);
    int32_t v1 = rs1Imm ? immValue(rs1) : 0;
    int32_t v2 = rs2Imm ? immValue(rs2) : 0;

    switch (inst.opcode) {
        case IROpcode::ADD:
            // x + 0 = x, 0 + x = x
            if (rs2Imm && v2 == 0)
                return SimplifiedInst{IROpcode::ADD, {rd, rs1, IROperand::imm(0)}};
            if (rs1Imm && v1 == 0)
                return SimplifiedInst{IROpcode::ADD, {rd, rs2, IROperand::imm(0)}};
            break;
        case IROpcode::SUB:
            // x - 0 = x, x - x = 0
            if (rs2Imm && v2 == 0)
                return SimplifiedInst{IROpcode::ADD, {rd, rs1, IROperand::imm(0)}};
            if (rs1.kind == OperandKind::VirtualReg &&
                rs2.kind == OperandKind::VirtualReg && regId(rs1) == regId(rs2))
                return SimplifiedInst{IROpcode::ADD, {rd, IROperand::imm(0), IROperand::imm(0)}};
            // const - x = -(x - const) handled by general constant folding
            break;
        case IROpcode::MUL:
            // x * 0 = 0, x * 1 = x
            if (rs2Imm && v2 == 0)
                return SimplifiedInst{IROpcode::ADD, {rd, IROperand::imm(0), IROperand::imm(0)}};
            if (rs1Imm && v1 == 0)
                return SimplifiedInst{IROpcode::ADD, {rd, IROperand::imm(0), IROperand::imm(0)}};
            if (rs2Imm && v2 == 1)
                return SimplifiedInst{IROpcode::ADD, {rd, rs1, IROperand::imm(0)}};
            if (rs1Imm && v1 == 1)
                return SimplifiedInst{IROpcode::ADD, {rd, rs2, IROperand::imm(0)}};
            break;
        case IROpcode::DIV:
            // x / 1 = x, 0 / x = 0
            if (rs2Imm && v2 == 1)
                return SimplifiedInst{IROpcode::ADD, {rd, rs1, IROperand::imm(0)}};
            if (rs1Imm && v1 == 0 && v2 != 0)
                return SimplifiedInst{IROpcode::ADD, {rd, IROperand::imm(0), IROperand::imm(0)}};
            break;
        case IROpcode::MOD:
            // x % 1 = 0, x % x = 0
            if (rs2Imm && v2 == 1)
                return SimplifiedInst{IROpcode::ADD, {rd, IROperand::imm(0), IROperand::imm(0)}};
            if (rs1.kind == OperandKind::VirtualReg &&
                rs2.kind == OperandKind::VirtualReg && regId(rs1) == regId(rs2))
                return SimplifiedInst{IROpcode::ADD, {rd, IROperand::imm(0), IROperand::imm(0)}};
            break;
        case IROpcode::AND:
            // x && 0 = 0, x && 1 = x
            if (rs2Imm && v2 == 0)
                return SimplifiedInst{IROpcode::ADD, {rd, IROperand::imm(0), IROperand::imm(0)}};
            if (rs1Imm && v1 == 0)
                return SimplifiedInst{IROpcode::ADD, {rd, IROperand::imm(0), IROperand::imm(0)}};
            if (rs2Imm && v2 != 0)
                return SimplifiedInst{IROpcode::ADD, {rd, rs1, IROperand::imm(0)}};
            if (rs1Imm && v1 != 0)
                return SimplifiedInst{IROpcode::ADD, {rd, rs2, IROperand::imm(0)}};
            break;
        case IROpcode::OR:
            // x || 0 = x, x || 1 = 1
            if (rs2Imm && v2 == 0)
                return SimplifiedInst{IROpcode::ADD, {rd, rs1, IROperand::imm(0)}};
            if (rs1Imm && v1 == 0)
                return SimplifiedInst{IROpcode::ADD, {rd, rs2, IROperand::imm(0)}};
            if (rs2Imm && v2 != 0)
                return SimplifiedInst{IROpcode::ADD, {rd, IROperand::imm(1), IROperand::imm(1)}};
            if (rs1Imm && v1 != 0)
                return SimplifiedInst{IROpcode::ADD, {rd, IROperand::imm(1), IROperand::imm(1)}};
            break;
        default:
            break;
    }
    return std::nullopt;
}

// ============================================================
// Pass 1：常量折叠 + 代数化简 + 拷贝传播（前向扫描）
// ============================================================

// 追踪的信息：常量值、拷贝来源
struct ValueInfo {
    // 如果已知常量值
    std::optional<int32_t> constant;
    // 如果是拷贝（ADD rd, rs, #0），记录来源寄存器
    std::optional<uint32_t> copyFrom;
};

void forwardOptimizePass(IRFunction& fn) {
    // 每个 vreg 的已知信息
    std::unordered_map<uint32_t, ValueInfo> values;
    std::vector<IRInstruction> newInsts;
    newInsts.reserve(fn.instructions.size());

    for (auto& inst : fn.instructions) {
        // --- 步骤 1：用已知常量替换操作数 ---
        // 注意：只替换源操作数（从索引 1 开始），避免替换目标寄存器
        // 例外：RET 指令的 operands[0] 是返回值（源操作数），需要替换
        size_t replaceStart = (inst.opcode == IROpcode::RET) ? 0 : 1;
        for (size_t i = replaceStart; i < inst.operands.size(); ++i) {
            auto& op = inst.operands[i];
            if (isReg(op)) {
                uint32_t r = regId(op);
                auto it = values.find(r);
                if (it != values.end() && it->second.constant.has_value()) {
                    op = IROperand::imm(*it->second.constant);
                }
            }
        }

        // --- 步骤 2：尝试常量折叠 ---
        bool folded = false;
        auto dest = destReg(inst);

        // 二元指令：OP rd, rs1, rs2
        if (inst.operands.size() == 3 && isImm(inst.operands[1]) && isImm(inst.operands[2])) {
            int32_t a = immValue(inst.operands[1]);
            int32_t b = immValue(inst.operands[2]);
            std::optional<int32_t> result;

            switch (inst.opcode) {
                case IROpcode::ADD: result = a + b; break;
                case IROpcode::SUB: result = a - b; break;
                case IROpcode::MUL: result = a * b; break;
                case IROpcode::DIV: if (b != 0) result = a / b; break;
                case IROpcode::MOD: if (b != 0) result = a % b; break;
                case IROpcode::LT:  result = (a < b) ? 1 : 0; break;
                case IROpcode::GT:  result = (a > b) ? 1 : 0; break;
                case IROpcode::LE:  result = (a <= b) ? 1 : 0; break;
                case IROpcode::GE:  result = (a >= b) ? 1 : 0; break;
                case IROpcode::EQ:  result = (a == b) ? 1 : 0; break;
                case IROpcode::NE:  result = (a != b) ? 1 : 0; break;
                case IROpcode::AND: result = (a && b) ? 1 : 0; break;
                case IROpcode::OR:  result = (a || b) ? 1 : 0; break;
                default: break;
            }

            if (result.has_value()) {
                // 替换为 ADD rd, #0, result（加载常量到寄存器）
                // 注意：后端为 ADD/SUB/MUL 等生成的代码使用 loadOperand，可以正确处理立即数
                // 所以我们直接生成带立即数的 ADD 让后端处理
                // 但为了更高效，我们仍然保存到 vreg 中（因为后面的指令可能需要读这个 vreg）
                newInsts.push_back({
                    IROpcode::ADD,
                    {inst.operands[0], IROperand::imm(0), IROperand::imm(*result)}
                });
                if (dest) values[*dest] = {*result, std::nullopt};
                folded = true;
            }
        }

        // 一元指令：OP rd, rs
        if (!folded && inst.operands.size() == 2 && isImm(inst.operands[1])) {
            int32_t v = immValue(inst.operands[1]);
            std::optional<int32_t> result;

            switch (inst.opcode) {
                case IROpcode::NEG: result = -v; break;
                case IROpcode::NOT: result = v ? 0 : 1; break;
                default: break;
            }

            if (result.has_value()) {
                newInsts.push_back({
                    IROpcode::ADD,
                    {inst.operands[0], IROperand::imm(0), IROperand::imm(*result)}
                });
                if (dest) values[*dest] = {*result, std::nullopt};
                folded = true;
            }
        }

        if (folded) continue;

        // --- 步骤 3：代数化简 ---
        auto simplified = simplifyAlgebraic(inst);
        if (simplified) {
            IRInstruction newInst = {simplified->opcode, simplified->operands};
            // 化简后如果 rd=rs1 且 rs2=0（拷贝），记录拷贝传播信息
            if (newInst.operands.size() == 3 &&
                isImm(newInst.operands[2]) && immValue(newInst.operands[2]) == 0 &&
                isReg(newInst.operands[1]) && dest) {
                uint32_t src = regId(newInst.operands[1]);
                values[*dest] = {std::nullopt, src};
            } else {
                // 非拷贝传播情况，清除之前的拷贝信息
                if (dest) values[*dest] = {std::nullopt, std::nullopt};
            }

            // 如果化简结果是 ADD rd, #0, #0（全零），记录常量
            if (newInst.operands.size() == 3 &&
                isImm(newInst.operands[1]) && immValue(newInst.operands[1]) == 0 &&
                isImm(newInst.operands[2]) && immValue(newInst.operands[2]) == 0 &&
                dest) {
                values[*dest] = {0, std::nullopt};
            }

            newInsts.push_back(std::move(newInst));
            continue;
        }

        // --- 步骤 4：拷贝传播（用源寄存器替换） ---
        if (inst.operands.size() == 3 &&
            isImm(inst.operands[2]) && immValue(inst.operands[2]) == 0 &&
            isReg(inst.operands[1]) && dest &&
            (inst.opcode == IROpcode::ADD || inst.opcode == IROpcode::SUB ||
             inst.opcode == IROpcode::MUL || inst.opcode == IROpcode::DIV)) {
            // 如果 rd 和 rs1 不同，这是拷贝：ADD rd, rs1, #0
            if (regId(inst.operands[1]) != *dest) {
                values[*dest] = {std::nullopt, regId(inst.operands[1])};
            }
        } else {
            // 其他情况：清除目标寄存器的拷贝/常量信息
            if (hasSideEffect(inst)) {
                // 分支指令清除所有值信息
                if (inst.opcode == IROpcode::BEQ || inst.opcode == IROpcode::BNE ||
                    inst.opcode == IROpcode::JMP) {
                    values.clear();
                }
            }
            // CALL/LOAD_GLOBAL/LOAD_LOCAL 产生非常量值
            if (inst.opcode == IROpcode::CALL ||
                inst.opcode == IROpcode::LOAD_GLOBAL ||
                inst.opcode == IROpcode::LOAD_LOCAL) {
                if (dest) values[*dest] = {std::nullopt, std::nullopt};
            }
        }

        newInsts.push_back(std::move(inst));
    }

    fn.instructions = std::move(newInsts);

    // --- 步骤 5：第二次扫描，进行拷贝传播（替换所有拷贝引用） ---
    // 构建最终的拷贝映射：对于每个 vreg，找到其最终来源
    std::unordered_map<uint32_t, uint32_t> copyMap;
    for (const auto& [reg, info] : values) {
        if (info.copyFrom.has_value()) {
            uint32_t src = *info.copyFrom;
            // 追踪拷贝链
            while (true) {
                auto it = values.find(src);
                if (it != values.end() && it->second.copyFrom.has_value()) {
                    src = *it->second.copyFrom;
                } else {
                    break;
                }
            }
            copyMap[reg] = src;
        }
    }

    // 应用拷贝传播
    // 注意：只替换源操作数（从索引 1 开始），避免修改目标寄存器
    // 例外：RET 指令的 operands[0] 是返回值（源操作数），需要替换
    for (auto& inst : fn.instructions) {
        size_t startIdx = (inst.opcode == IROpcode::RET) ? 0 : 1;
        for (size_t i = startIdx; i < inst.operands.size(); ++i) {
            auto& op = inst.operands[i];
            if (isReg(op)) {
                uint32_t r = regId(op);
                auto it = copyMap.find(r);
                if (it != copyMap.end()) {
                    op = IROperand::reg(it->second);
                }
            }
        }
    }
}

// ============================================================
// Pass 2：死代码消除（活跃分析 + 后向扫描）
// ============================================================

void deadCodeElimination(IRFunction& fn) {
    // 收集所有被"使用"的虚拟寄存器
    std::unordered_set<uint32_t> liveRegs;

    // 第一遍：标记所有被源操作数引用的寄存器
    for (const auto& inst : fn.instructions) {
        // 对于 RET，operands[0] 是源操作数（返回值），不是目标
        if (inst.opcode == IROpcode::RET) {
            if (!inst.operands.empty() && inst.operands[0].kind == OperandKind::VirtualReg) {
                liveRegs.insert(regId(inst.operands[0]));
            }
            continue;
        }
        // 对于其他指令，operands[0] 是目标寄存器，从索引 1 开始是源操作数
        for (size_t i = 1; i < inst.operands.size(); ++i) {
            if (inst.operands[i].kind == OperandKind::VirtualReg) {
                liveRegs.insert(regId(inst.operands[i]));
            }
        }
    }

    // 第二遍：过滤指令
    std::vector<IRInstruction> newInsts;
    newInsts.reserve(fn.instructions.size());

    for (auto& inst : fn.instructions) {
        auto dest = destReg(inst);
        bool keep = true;

        if (dest.has_value() && !hasSideEffect(inst)) {
            // 如果目标寄存器不被使用，删除该指令
            if (liveRegs.find(*dest) == liveRegs.end()) {
                keep = false;
            }
        }

        if (keep) {
            newInsts.push_back(std::move(inst));
        }
    }

    fn.instructions = std::move(newInsts);
}

// ============================================================
// Pass 3：Store-Load 传播
// 追踪每个栈偏移的最新 STORE_LOCAL 值
// 当 LOAD_LOCAL rd, #off 时，若最近有 STORE_LOCAL #off, rs，将 rd 替换为 rs
// ============================================================

void storeLoadForwarding(IRFunction& fn) {
    // 追踪每个栈偏移的最新 vreg
    std::unordered_map<int32_t, uint32_t> latestStore;
    std::vector<IRInstruction> newInsts;
    newInsts.reserve(fn.instructions.size());

    for (auto& inst : fn.instructions) {
        if (inst.opcode == IROpcode::STORE_LOCAL && inst.operands.size() >= 2) {
            // STORE_LOCAL #off, rs
            if (isImm(inst.operands[0]) && isReg(inst.operands[1])) {
                int32_t off = immValue(inst.operands[0]);
                uint32_t src = regId(inst.operands[1]);
                latestStore[off] = src;
            }
            newInsts.push_back(std::move(inst));
        } else if (inst.opcode == IROpcode::LOAD_LOCAL && inst.operands.size() >= 2) {
            // LOAD_LOCAL rd, #off — 查找是否有刚存的同偏移值
            if (isImm(inst.operands[1]) && isReg(inst.operands[0])) {
                int32_t off = immValue(inst.operands[1]);
                auto it = latestStore.find(off);
                if (it != latestStore.end()) {
                    // 用 rs 替换 rd — 这是一个拷贝
                    uint32_t rd = regId(inst.operands[0]);
                    uint32_t rs = it->second;
                    if (rd != rs) {
                        // 生成 ADD rd, rs, #0（拷贝）
                        newInsts.push_back({
                            IROpcode::ADD,
                            {IROperand::reg(rd), IROperand::reg(rs), IROperand::imm(0)}
                        });
                        continue;
                    }
                }
            }
            // 没有命中，清除该栈偏移的追踪（因为加载可能来自其他执行路径）
            if (inst.operands.size() >= 2 && isImm(inst.operands[1])) {
                int32_t off = immValue(inst.operands[1]);
                latestStore.erase(off);
            }
            newInsts.push_back(std::move(inst));
        } else {
            // CALL 和分支指令会清空所有追踪（可能改变局部变量）
            if (inst.opcode == IROpcode::CALL ||
                inst.opcode == IROpcode::BEQ ||
                inst.opcode == IROpcode::BNE ||
                inst.opcode == IROpcode::JMP) {
                latestStore.clear();
            }
            newInsts.push_back(std::move(inst));
        }
    }

    fn.instructions = std::move(newInsts);
}

// ============================================================
// Pass 4：循环不变式外提
// ============================================================

// 简单分析：找出循环结构并将循环内不变的计算外提到循环前
// 只处理 while 循环模式：LABEL cond → 条件检查 → BEQ/BNE to end → body... → JMP cond → LABEL end
void loopInvariantCodeMotion(IRFunction& fn) {
    auto& insts = fn.instructions;
    if (insts.empty()) return;

    // 寻找循环结构：LABEL L_cond ... (条件判断) BNE/BEQ ... JMP L_cond ... LABEL L_end
    // 简化为：查找所有 LABEL → JMP 回该 LABEL 的模式

    // 先找出所有 label 的位置
    std::unordered_map<uint32_t, size_t> labelPositions;
    for (size_t i = 0; i < insts.size(); ++i) {
        if (insts[i].opcode == IROpcode::LABEL && !insts[i].operands.empty() &&
            insts[i].operands[0].kind == OperandKind::Label) {
            labelPositions[regId(insts[i].operands[0])] = i;
        }
    }

    // 对于每个 JMP (无条件跳转到 label)，检查是否构成循环
    // 循环模式：JMP L_back (向后跳转) 且 L_back 在前方
    // 真正的循环：condLabel → 条件检查 → BNE bodyLabel → JMP endLabel → bodyLabel → ... → JMP condLabel → endLabel
    // 我们简化处理：查找从 condLabel 到最终 JMP condLabel 的区域

    // 收集所有向后跳转的 JMP（可能的循环回边）
    // 由于我们需要修改指令列表，使用索引操作

    bool changed = true;
    int pass = 0;
    const int maxPasses = 10;

    while (changed && pass < maxPasses) {
        changed = false;
        pass++;

        // 重新计算 label 位置
        labelPositions.clear();
        for (size_t i = 0; i < insts.size(); ++i) {
            if (insts[i].opcode == IROpcode::LABEL && !insts[i].operands.empty() &&
                insts[i].operands[0].kind == OperandKind::Label) {
                labelPositions[regId(insts[i].operands[0])] = i;
            }
        }

        // 找循环：JMP (向后跳转到 label)
        for (size_t i = 0; i < insts.size(); ++i) {
            if (insts[i].opcode != IROpcode::JMP) continue;
            if (insts[i].operands.empty() || insts[i].operands[0].kind != OperandKind::Label)
                continue;

            uint32_t targetLabel = regId(insts[i].operands[0]);
            auto labelIt = labelPositions.find(targetLabel);
            if (labelIt == labelPositions.end()) continue;
            size_t labelPos = labelIt->second;

            // 只处理向后跳转（循环）
            if (labelPos >= i) continue;

            // 找到循环体 [labelPos, i]
            // 循环条件在 labelPos 之后、循环体之前
            // 简化：将 labelPos 到 i 之间的指令视为循环区域

            // 找出循环内不变的指令（操作数在循环外定义或为常量）
            // 循环中定义的寄存器集合
            std::unordered_set<uint32_t> definedInLoop;
            for (size_t j = labelPos; j <= i; ++j) {
                auto d = destReg(insts[j]);
                if (d) definedInLoop.insert(*d);
            }

            // 找出循环内可外提的指令
            // 条件：指令目标寄存器不被循环内其他指令使用（除自身外），
            // 且所有源操作数在循环外定义（或为立即数）
            std::vector<size_t> hoistCandidates;
            for (size_t j = labelPos + 1; j < i; ++j) {
                const auto& inst = insts[j];
                auto d = destReg(inst);
                if (!d || hasSideEffect(inst)) continue;

                // 检查源操作数是否都在循环外定义
                bool allSrcOutsideLoop = true;
                for (size_t k = 1; k < inst.operands.size(); ++k) {
                    const auto& op = inst.operands[k];
                    if (op.kind == OperandKind::VirtualReg) {
                        uint32_t srcReg = regId(op);
                        if (definedInLoop.find(srcReg) != definedInLoop.end()) {
                            allSrcOutsideLoop = false;
                            break;
                        }
                    }
                }

                if (allSrcOutsideLoop) {
                    hoistCandidates.push_back(j);
                }
            }

            // 外提候选指令到循环前（labelPos 之前）
            if (!hoistCandidates.empty()) {
                // 从后往前外提（保持顺序）
                std::sort(hoistCandidates.begin(), hoistCandidates.end(), std::greater<size_t>());
                for (size_t idx : hoistCandidates) {
                    IRInstruction hoisted = std::move(insts[idx]);
                    insts.erase(insts.begin() + static_cast<ptrdiff_t>(idx));
                    insts.insert(insts.begin() + static_cast<ptrdiff_t>(labelPos), std::move(hoisted));
                    changed = true;
                }
                // 因为修改了 insts，重新开始
                break;
            }
        }
    }
}

// ============================================================
// Pass 4：强度削弱（循环内的乘法/除法优化）
// ============================================================

void strengthReduction(IRFunction& fn) {
    // 简单的强度削弱：
    // x * 2 → x + x
    // x * 4 → x + x + x + x (用两次 ADD)
    // x / 2 → x >> 1 (但 RISC-V 后端不支持 SRL 立即数，所以暂不处理)
    // 实际上 RISC-V 有 mul 指令，强度削弱可能效果不大
    // 主要处理：x * 常数 且常数为 2 的幂 → 通过 ADD 链实现
    // 暂时跳过，因为 RISC-V 的 mul 已经足够高效
    (void)fn;
}

// ============================================================
// 主优化函数
// ============================================================

}  // namespace

IRProgram Optimizer::optimize(const IRProgram& input) {
    IRProgram output = input;

    for (auto& fn : output.functions) {
        // 迭代执行 Pass 直到收敛
        for (int iter = 0; iter < 5; ++iter) {
            size_t beforeSize = fn.instructions.size();

            // Pass 1: 前向优化（常量折叠 + 代数化简 + 拷贝传播）
            forwardOptimizePass(fn);

            // Pass 2: 死代码消除
            deadCodeElimination(fn);

            // 如果指令数不再减少，收敛
            if (fn.instructions.size() == beforeSize) break;
        }

        // Pass 3: Store-Load 传播
        storeLoadForwarding(fn);

        // 再次迭代优化（Store-Load 传播可能暴露新的优化机会）
        for (int iter = 0; iter < 3; ++iter) {
            size_t beforeSize = fn.instructions.size();
            forwardOptimizePass(fn);
            deadCodeElimination(fn);
            if (fn.instructions.size() == beforeSize) break;
        }

        // Pass 4: 循环不变式外提
        loopInvariantCodeMotion(fn);

        // 再次迭代优化（循环外提可能暴露新的优化机会）
        for (int iter = 0; iter < 3; ++iter) {
            size_t beforeSize = fn.instructions.size();
            forwardOptimizePass(fn);
            deadCodeElimination(fn);
            storeLoadForwarding(fn);
            forwardOptimizePass(fn);
            deadCodeElimination(fn);
            if (fn.instructions.size() == beforeSize) break;
        }
    }

    return output;
}

}  // namespace toyc