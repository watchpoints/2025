///
/// @file CodeGeneratorArm64.cpp
/// @brief ARM64的后端处理实现
/// @author zenglj (zenglj@live.com)
/// @version 1.0
/// @date 2024-11-21
///
/// @copyright Copyright (c) 2024
///
/// @par 修改日志:
/// <table>
/// <tr><th>Date       <th>Version <th>Author  <th>Description
/// <tr><td>2024-11-21 <td>1.0     <td>zenglj  <td>新做
/// </table>
///
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

#include "Function.h"
#include "Module.h"
//#include "PlatformArm64.h"
#include "CodeGeneratorArm64.h"
#include "InstSelectorArm64.h"
//#include "SimpleRegisterAllocator.h"
#include "ILocArm64.h"
//#include "RegVariable.h"
#include "FuncCallInstruction.h"
#include "ArgInstruction.h"
#include "MoveInstruction.h"
#include "PlatformArm64.h"
#include "ArrayType.h"

#define DEBUG 1
#ifdef DEBUG
#define PNT(fmt, ...) fprintf(stderr, fmt, __VA_ARGS__)
#else
#define PNT(fmt, ...)
#endif

static int allocateStackSlot(Function *, Type *);
static void expireOldRanges(std::vector<LiveRange> &active, std::vector<int> &freeRegs, int pos);
static int findLastUse(Value *val, const std::vector<Instruction*> &insts, int startPos);
static void extendRangeIfExists(std::vector<LiveRange> &ranges, Value *value, int currentPos);
static const std::vector<LiveRange> &calculateLiveRanges(Function *func);

/// @brief 构造函数
/// @param tab 符号表
CodeGeneratorArm64::CodeGeneratorArm64(Module * _module)
: CodeGeneratorAsm(_module), simpleRegisterAllocator(PlatformArm64::maxUsableRegNum)
{
    ConstInt::setZeroReg(ARM64_ZR_REG_NO);
}

/// @brief 析构函数
CodeGeneratorArm64::~CodeGeneratorArm64()
{
    ConstInt::setZeroReg(-1);
}

/// @brief 产生汇编头部分
void CodeGeneratorArm64::genHeader()
{
    // 定义算余数的宏
    fputs(".macro rem dst, divd, divr\n"
          "sdiv \\dst, \\divd, \\divr\n"
          "msub \\dst, \\dst, \\divr, \\divd\n"
          ".endm\n", fp);
}

/// @brief 全局变量Section，主要包含初始化的和未初始化过的
void CodeGeneratorArm64::genDataSection()
{

    // 可直接操作文件指针fp进行写操作

    // 目前不支持全局变量和静态变量，以及字符串常量
    // 全局变量分两种情况：初始化的全局变量和未初始化的全局变量
    // TODO 这里先处理未初始化的全局变量
    for (auto var: module->getGlobalVariables()) {

        if (var->isInBSSSection()) {

            // 在BSS段的全局变量，可以包含初值全是0的变量
            fprintf(fp, ".comm %s, %d, %d\n", var->getName().c_str(), var->getType()->getSize(), var->getAlignment());
        } else {

            // 有初值的全局变量
            fprintf(fp, ".type %s, @object\n", var->getName().c_str());
            fputs(".data\n", fp);
            fprintf(fp, ".globl %s\n", var->getName().c_str());
            fprintf(fp, ".align 2\n");
            fprintf(fp, "%s:\n", var->getName().c_str());
            fprintf(fp, ".word 0x%x\n", var->intVal);
            // TODO 数组初值
        }
    }
}

///
/// @brief 获取IR变量相关信息字符串
/// @param str
///
void CodeGeneratorArm64::getIRValueStr(Value * val, std::string & str)
{
    std::string name = val->getName();
    std::string IRName = val->getIRName();
    int32_t regId = val->getRegId();
    int32_t baseRegId;
    int64_t offset;
    std::string showName;

    if (name.empty() && (!IRName.empty())) {
        showName = IRName;
    } else if ((!name.empty()) && IRName.empty()) {
        showName = name;
    } else if ((!name.empty()) && (!IRName.empty())) {
        showName = name + ":" + IRName;
    } else {
        showName = "";
    }

    if (regId != -1) {
        // 寄存器
        str += "\t@ " + showName + ":" + PlatformArm64::regName[regId];
    } else if (val->getMemoryAddr(&baseRegId, &offset)) {
        // 栈内寻址，[fp,#4]
        str += "\t@ " + showName + ":[" + PlatformArm64::regName[baseRegId] + ",#" + std::to_string(offset) + "]";
    }
}

/// @brief 针对函数进行汇编指令生成，放到.text代码段中
/// @param func 要处理的函数
void CodeGeneratorArm64::genCodeSection(Function * func)
{
    // 生成代码段
    fputs(".text\n", fp);
    // 寄存器分配以及栈内局部变量的站内地址重新分配
    registerAllocation(func);

    // 获取函数的指令列表
    std::vector<Instruction *> & IrInsts = func->getInterCode().getInsts();

    // 汇编指令输出前要确保Label的名字有效，必须是程序级别的唯一，而不是函数内的唯一。要全局编号。
    for (auto inst: IrInsts) {
        if (inst->getOp() == IRInstOperator::IRINST_OP_LABEL) {
            inst->setName(IR_LABEL_PREFIX + std::to_string(labelIndex++));
        }
    }

    // ILOC代码序列
    ILocArm64 iloc(module);

    // 指令选择生成汇编指令
    InstSelectorArm64 instSelector(IrInsts, iloc, func, simpleRegisterAllocator);
    instSelector.setShowLinearIR(false);
    //this->showLinearIR);
    instSelector.run();

    // 删除无用的Label指令
    iloc.deleteUsedLabel();

    // ILOC代码输出为汇编代码
    fprintf(fp, ".align 2\n"); // 函数统一按2对齐
    fprintf(fp, ".globl %s\n", func->getName().c_str());
    fprintf(fp, ".type %s, @function\n", func->getName().c_str());
    fprintf(fp, "%s:\n", func->getName().c_str());

    // 开启时输出IR指令作为注释
    if (this->showLinearIR) {

        // 输出有关局部变量的注释，便于查找问题
        for (auto localVar: func->getVarValues()) {
            std::string str;
            getIRValueStr(localVar, str);
            if (!str.empty()) {
                fprintf(fp, "%s\n", str.c_str());
            }
        }

        // 输出指令关联的临时变量信息
        for (auto inst: func->getInterCode().getInsts()) {
            if (inst->hasResultValue()) {
                std::string str;
                getIRValueStr(inst, str);
                if (!str.empty()) {
                    fprintf(fp, "%s\n", str.c_str());
                }
            }
        }
    }

    iloc.outPut(fp);
}

/// @brief 寄存器分配
/// @param func 函数指针
void CodeGeneratorArm64::registerAllocation(Function * func)
{
    // 内置函数不需要处理
    if (func->isBuiltin()) return;

    // 最简单/朴素的寄存器分配策略：局部变量和临时变量都保存在栈内，全局变量在静态存储.data区中
    // R0,R1,R2和R3寄存器不需要保护，可直接使用
    // SP寄存器预留，不需要保护，但需要保证值的正确性
    // R4-R10, fp(11), lx(14)都需要保护，没有函数调用的函数可不用保护lx寄存器
    // 被保留的寄存器主要有：
    //  (1) FP寄存器用于栈寻址，x29
    //  (2) LR寄存器用于函数调用，即x30。没有函数调用的函数可不用保护lx寄存器
    //  (3) R10寄存器用于立即数过大时要通过寄存器寻址，这里简化处理进行预留

    std::vector<int32_t> & protectedRegNo = func->getProtectedReg();
    // protectedRegNo.push_back(ARM32_TMP_REG_NO);
    protectedRegNo.push_back(ARM64_FP_REG_NO);
    if (func->getExistFuncCall()) {
        protectedRegNo.push_back(ARM64_LR_REG_NO);
    }

    // 1. 计算活跃区间
    std::vector<LiveRange> ranges = calculateLiveRanges(func);

    // 2. 按起始位置排序
    std::sort(ranges.begin(), ranges.end(), 
        [](const LiveRange &a, const LiveRange &b) { return a.start < b.start; });

    // 3. 分配寄存器
    linearScanRegisterAllocation(ranges, func);

    // 4. 处理剩余逻辑（如保护寄存器）
    adjustFuncCallInsts(func);

    // 5. 对齐sp
    int dep = func->getMaxDep();
    func->setMaxDep((dep + 15) & ~15);

    adjustFormalParamInsts(func);

#if 0
    // 临时输出调整后的IR指令，用于查看当前的寄存器分配、栈内变量分配、实参入栈等信息的正确性
    std::string irCodeStr;
    func->toString(irCodeStr);
    std::cout << irCodeStr << std::endl;
#endif
}

/// @brief 寄存器分配前对函数内的指令进行调整，以便方便寄存器分配
/// @param func 要处理的函数
/// @param usedArgs 调用其余函数时最大实参数
void CodeGeneratorArm64::adjustFormalParamInsts(Function * func)
{
    // 请注意这里所得的所有形参都是对应的实参的值关联的临时变量
    // 如果不是不能使用这里的代码
    auto & params = func->getParams();

    // 形参的前8个通过寄存器来传值R0-R7
    // 先排除需要函数调用的参数，这些参数需要重新分配寄存器
    //int k = std::max((int)func->getExistFuncCall(), func->getMaxFuncCallArgCnt());
    int pm = func->getExistFuncCall()?params.size():0;
    int k;
    std::vector<Instruction*> moves(std::min(8, pm));
    for (k = 0; k < std::min(8, pm); k++) {
        moves[k] = new MoveInstruction(func, params[k], PlatformArm64::intRegVal[k]);
    }
    auto &insts = func->getInterCode().getInsts();
    insts.insert(insts.begin()+1, moves.begin(), moves.end());

    auto &protects = func->getProtectedReg();
    pm = params.size();

    for (int j = std::min(pm, 8); k < j; k++) {

        // 前8个设置分配寄存器
        int32_t reg = params[k]->getRegId();
        if (ARM64_CALLER_SAVE(reg)) {
            std::remove(protects.begin(), protects.end(), reg);
        }
        params[k]->setRegId(k);
    }

    // 根据ARM64版C语言的调用约定，除前8个外的实参进行值传递，逆序入栈
    int64_t fp_esp = func->getMaxDep() + (func->getProtectedReg().size() * 4);
    for (; k < pm; k++) {

        // 目前假定变量大小都是4字节。实际要根据类型来计算
        int32_t reg = params[k]->getRegId();
        if (ARM64_CALLER_SAVE(reg)) {
            std::remove(protects.begin(), protects.end(), reg);
            params[k]->setRegId(-1);
        }
        params[k]->setMemoryAddr(ARM64_FP_REG_NO, fp_esp);

        // 增加4字节
        fp_esp += 4;
    }
}

/// @brief 寄存器分配前对函数内的指令进行调整，以便方便寄存器分配
/// @param func 要处理的函数
void CodeGeneratorArm64::adjustFuncCallInsts(Function * func)
{
    std::vector<Instruction *> newInsts;

    // 当前函数的指令列表
    auto & insts = func->getInterCode().getInsts();

    // 函数返回值用R0寄存器，若函数调用有返回值，则赋值R0到对应寄存器
    for (auto pIter = insts.begin(); pIter != insts.end(); pIter++) {

        // 检查是否是函数调用指令，并且含有返回值
        if (Instanceof(callInst, FuncCallInstruction *, *pIter)) {

            // 实参前8个要寄存器传值，其它参数通过栈传递
            // 前8个的后面参数采用栈传递
            int esp = 0;
            for (int32_t k = 8, l = callInst->getOperandsNum(); k < l; k++) {

                auto arg = callInst->getOperand(k);
                if (arg == callInst) break;

                // 新建一个内存变量，用于栈传值到形参变量中
                LocalVariable * newVal = func->newLocalVarValue(IntegerType::getTypeInt());
                newVal->setMemoryAddr(ARM64_SP_REG_NO, esp);
                esp += 4;

                Instruction * assignInst = new MoveInstruction(func, newVal, arg);

                callInst->setOperand(k, newVal);

                // 函数调用指令前插入后，pIter仍指向函数调用指令
                pIter = insts.insert(pIter, assignInst);
                pIter++;
            }

            for (int k = 0, l = std::min(callInst->getOperandsNum(), 8); k < l; k++) {

                // 检查实参的类型是否是临时变量。
                // 如果是临时变量，该变量可更改为寄存器变量即可，或者设置寄存器号
                // 如果不是，则必须开辟一个寄存器变量，然后赋值即可
                auto arg = callInst->getOperand(k);
                if (arg == callInst) break;

                if (arg->getRegId() == k) {
                    // 则说明寄存器已经是实参传递的寄存器，不用创建赋值指令
                    continue;
                } else {
                    // 创建临时变量，指定寄存器

                    Instruction * assignInst =
                        new MoveInstruction(func, PlatformArm64::intRegVal[k], callInst->getOperand(k));

                    callInst->setOperand(k, PlatformArm64::intRegVal[k]);

                    // 函数调用指令前插入后，pIter仍指向函数调用指令
                    pIter = insts.insert(pIter, assignInst);
                    pIter++;
                }
            }

            for (int k = 0; k < callInst->getOperandsNum(); k++) {
                auto arg = callInst->getOperand(k);
                if (arg == callInst) continue;

                // 再产生ARG指令
                pIter = insts.insert(pIter, new ArgInstruction(func, arg));
                pIter++;
            }

            // 有arg指令后可不用参数，展示不删除
            // args.clear();

            // 赋值指令
            if (callInst->hasResultValue()) {

                if (callInst->getRegId() == 0) {
                    // 结果变量的寄存器和返回值寄存器一样，则什么都不需要做
                    ;
                } else {
                    // 其它情况，需要产生赋值指令
                    // 新建一个赋值操作
                    Instruction * assignInst = new MoveInstruction(func, callInst, PlatformArm64::intRegVal[0]);

                    // 函数调用指令的下一个指令的前面插入指令，因为有Exit指令，+1肯定有效
                    pIter = insts.insert(pIter + 1, assignInst);
                }
            }
        }
    }
}

/// @brief 栈空间分配
/// @param func 要处理的函数
void CodeGeneratorArm64::stackAlloc(Function * func)
{
    // 遍历函数内的所有指令，查找没有寄存器分配的变量，然后进行栈内空间分配

    // 这里对临时变量和局部变量都在栈上进行分配,但形参对应实参的临时变量(FormalParam类型)不需要考虑

    int32_t sp_esp = 0;

    // 获取函数变量列表
    std::vector<LocalVariable *> & vars = func->getVarValues();

    for (auto var: vars) {

        // 对于简单类型的寄存器分配策略，假定临时变量和局部变量都保存在栈中，属于内存
        // 而对于图着色等，临时变量一般是寄存器，局部变量也可能修改为寄存器
        // TODO 考虑如何进行分配使得临时变量尽量保存在寄存器中，作为优化点考虑

        // regId不为-1，则说明该变量分配为寄存器
        // baseRegNo不等于-1，则说明该变量肯定在栈上，属于内存变量，之前肯定已经分配过
        if ((var->getRegId() == -1) && (!var->getMemoryAddr())) {

            // 该变量没有分配寄存器

            int32_t size = var->getType()->getSize();

            // 32位ARM平台按照4字节的大小整数倍分配局部变量
            size += (4 - size % 4) % 4;

            // 这里要注意检查变量栈的偏移范围。一般采用机制寄存器+立即数方式间接寻址
            // 若立即数满足要求，可采用基址寄存器+立即数变量的方式访问变量
            // 否则，需要先把偏移量放到寄存器中，然后机制寄存器+偏移寄存器来寻址
            // 之后需要对所有使用到该Value的指令在寄存器分配前要变换。

            // 局部变量偏移设置
            var->setMemoryAddr(ARM64_FP_REG_NO, sp_esp);

            // 累计当前作用域大小
            sp_esp += size;
        }
    }

    // 遍历指令中临时变量
    for (auto inst: func->getInterCode().getInsts()) {

        if (inst->hasResultValue() && inst->getRegId()==-1) {
            // 有值

            int32_t size = inst->getType()->getSize();

            // int、float等基本类型均按照4字节对其
            size += (4 - size % 4) % 4;

            // 这里要注意检查变量栈的偏移范围。一般采用机制寄存器+立即数方式间接寻址
            // 若立即数满足要求，可采用基址寄存器+立即数变量的方式访问变量
            // 否则，需要先把偏移量放到寄存器中，然后机制寄存器+偏移寄存器来寻址
            // 之后需要对所有使用到该Value的指令在寄存器分配前要变换。

            // 局部变量偏移设置
            inst->setMemoryAddr(ARM64_FP_REG_NO, sp_esp);

            // 累计当前作用域大小
            sp_esp += size;
        }
    }

    // 设置函数的最大栈帧深度，在加上实参内存传值的空间
    // sp必须按照16对齐
    sp_esp = (sp_esp + 15) & 0xFFFFFFF0;
    func->setMaxDep(sp_esp);
}

const std::vector<LiveRange> &calculateLiveRanges(Function *func) {
    auto ranges = new std::vector<LiveRange>();
    const auto &insts = func->getInterCode().getInsts();

    // 遍历指令，记录变量的定义和使用位置
    for (int pos = 0, l = insts.size(); pos < l; ++pos) {
        Instruction *inst = insts[pos];
        
        // 处理定义（赋值）
        if (inst->hasResultValue()) {
            LiveRange range;
            range.value = inst;
            range.start = pos;
            range.end = findLastUse(inst, insts, pos);
            ranges->push_back(range);
        }

        // 处理操作数（使用）
        for (int i = 0; i < inst->getOperandsNum(); ++i) {
            Value *operand = inst->getOperand(i);
            if (operand==inst) continue;
            if (dynamic_cast<Instruction*>(operand)
             || dynamic_cast<LocalVariable*>(operand)
             || dynamic_cast<FormalParam*>(operand)) {
                extendRangeIfExists(*ranges, operand, pos);
            }
        }
    }
    return *ranges;
}

void CodeGeneratorArm64::linearScanRegisterAllocation(
    std::vector<LiveRange> &ranges, Function *func) 
{
    // 使用被调用者保留寄存器
    std::vector<int32_t> freeRegs = {19, 20, 21, 22, 23, 24, 25, 26, 27, 28}; // x9-x15
    if (!func->getExistFuncCall()) {
        freeRegs.insert(freeRegs.end(), {9, 10, 11, 12, 13, 14, 15}); // x9-x15
    }
    std::vector<LiveRange> active;
    auto &protects = func->getProtectedReg();

    for (auto &range : ranges) {
        // 1. 过期已结束的区间
        expireOldRanges(active, freeRegs, range.start);

        // 2. 分配寄存器
        if (!(range.value->getType()->isArrayType() || freeRegs.empty())) {
            range.reg = freeRegs.back();
            freeRegs.pop_back();
            active.push_back(range);
        } else {
            // 3. 溢出到栈
            range.stackOffset = allocateStackSlot(func, range.value->getType());
        }
    }

    // 4. 更新变量的寄存器或栈偏移
    for (const auto &range : ranges) {
        if (range.reg != -1) {
            range.value->setRegId(range.reg);
            if (ARM64_CALLER_SAVE(range.reg) && std::find(protects.begin(), protects.end(), range.reg)==protects.end())
                protects.push_back(range.reg);
        } else {
            range.value->setMemoryAddr(ARM64_FP_REG_NO, range.stackOffset);
        }
    }
}

// 查找变量的最后一次使用
int findLastUse(Value *val, const std::vector<Instruction*> &insts, int startPos) {
    for (int i = insts.size() - 1; i >= startPos; --i) {
        for (int j = 0; j < insts[i]->getOperandsNum(); ++j) {
            if (insts[i]->getOperand(j) == val) {
                return i;
            }
        }
    }
    return startPos;
}

// 释放已结束的区间
void expireOldRanges(std::vector<LiveRange> &active, std::vector<int> &freeRegs, int pos) {
    for (auto it = active.begin(); it != active.end(); ) {
        if (it->end <= pos) {
            freeRegs.push_back(it->reg);
            it = active.erase(it);
        } else {
            ++it;
        }
    }
}

// 分配栈槽
int allocateStackSlot(Function *func, Type *type) {
    int offset = func->getMaxDep();
    func->setMaxDep(offset + type->getSize()); // 假设4字节对齐
    return offset;
}

void extendRangeIfExists(std::vector<LiveRange> &ranges, Value *value, int currentPos) {
    for (auto &range : ranges) {
        if (range.value == value) {
            // 扩展活跃范围到当前指令位置
            range.end = std::max(range.end, currentPos);
            return;
        }
    }
    // 如果未找到匹配的LiveRange，新的定义
    //Instanceof(cz, ConstInt*, value);
    if (!dynamic_cast<ConstInt*>(value)) {
        LiveRange lr;
        lr.value = value;
        lr.start = dynamic_cast<FormalParam*>(value)==nullptr?currentPos:0;
        lr.end = currentPos;
        ranges.push_back(lr);
    }
}
