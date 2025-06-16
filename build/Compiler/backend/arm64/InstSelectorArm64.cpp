///
/// @file InstSelectorArm64.cpp
/// @brief 指令选择器-ARM64的实现
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
#include <cstdio>

#include "CastInstruction.h"
#include "Common.h"
#include "ILocArm64.h"
#include "InstSelectorArm64.h"
#include "PlatformArm64.h"

#include "PointerType.h"
// #include "RegVariable.h"
#include "Function.h"

#include "LabelInstruction.h"
#include "GotoInstruction.h"
#include "FuncCallInstruction.h"
#include "MoveInstruction.h"
#include "ArrayType.h"
// #include "BinaryInstruction.h"

static char * cmpmap[] = {"eq", "ne", "gt", "le", "ge", "lt"};
#define CSTRJ(C) cmpmap[(C - IRINST_OP_IEQ) ^ 1]
#define CSTR(C) cmpmap[(C - IRINST_OP_IEQ)]

using std::to_string;
// static GotoInstruction *lastBranch;
/// @brief 构造函数
/// @param _irCode 指令
/// @param _iloc ILoc
/// @param _func 函数
InstSelectorArm64::InstSelectorArm64(vector<Instruction *> & _irCode,
                                     ILocArm64 & _iloc,
                                     Function * _func,
                                     SimpleRegisterAllocator & allocator)
    : ir(_irCode), iloc(_iloc), func(_func), simpleRegisterAllocator(allocator)
{
    translator_handlers[IRINST_OP_ENTRY] = &InstSelectorArm64::translate_entry;
    translator_handlers[IRINST_OP_EXIT] = &InstSelectorArm64::translate_exit;

    translator_handlers[IRINST_OP_LABEL] = &InstSelectorArm64::translate_label;
    translator_handlers[IRINST_OP_GOTO] = &InstSelectorArm64::translate_goto;

    translator_handlers[IRINST_OP_ASSIGN] = &InstSelectorArm64::translate_assign;

    translator_handlers[IROP(IADD)] = &InstSelectorArm64::translate_add_int32;
    translator_handlers[IROP(ISUB)] = &InstSelectorArm64::translate_sub_int32;
    translator_handlers[IROP(IMUL)] = &InstSelectorArm64::translate_mul_int32;
    translator_handlers[IROP(IDIV)] = &InstSelectorArm64::translate_div_int32;
    translator_handlers[IROP(IMOD)] = &InstSelectorArm64::translate_rem_int32;

    translator_handlers[IRINST_OP_FUNC_CALL] = &InstSelectorArm64::translate_call;
    translator_handlers[IRINST_OP_ARG] = &InstSelectorArm64::translate_arg;

    translator_handlers[IRINST_OP_IEQ] = &InstSelectorArm64::translate_bi_op;
    translator_handlers[IRINST_OP_INE] = &InstSelectorArm64::translate_bi_op;
    translator_handlers[IRINST_OP_IGT] = &InstSelectorArm64::translate_bi_op;
    translator_handlers[IRINST_OP_IGE] = &InstSelectorArm64::translate_bi_op;
    translator_handlers[IRINST_OP_ILT] = &InstSelectorArm64::translate_bi_op;
    translator_handlers[IRINST_OP_ILE] = &InstSelectorArm64::translate_bi_op;

    translator_handlers[IRINST_OP_FADD] = &InstSelectorArm64::translate_fadd;
    translator_handlers[IRINST_OP_FSUB] = &InstSelectorArm64::translate_fsub;
    translator_handlers[IRINST_OP_FMUL] = &InstSelectorArm64::translate_fmul;
    translator_handlers[IRINST_OP_FDIV] = &InstSelectorArm64::translate_fdiv;
    translator_handlers[IRINST_OP_FMOD] = &InstSelectorArm64::translate_fmod;

    translator_handlers[IRINST_OP_GEP] = &InstSelectorArm64::translate_gep;
    translator_handlers[IRINST_OP_STORE] = &InstSelectorArm64::translate_store;
    translator_handlers[IRINST_OP_LOAD] = &InstSelectorArm64::translate_load;

    translator_handlers[IRINST_OP_CAST] = &InstSelectorArm64::translate_cast;

    translator_handlers[IRINST_OP_XOR] = &InstSelectorArm64::translate_xor_int32;
}

///
/// @brief 析构函数
///
InstSelectorArm64::~InstSelectorArm64()
{}

/// @brief 指令选择执行
void InstSelectorArm64::run()
{
    for (auto inst: ir) {

        // 逐个指令进行翻译
        if (!inst->isDead()) {
            translate(inst);
        }
    }
}

/// @brief 指令翻译成ARM64汇编
/// @param inst IR指令
void InstSelectorArm64::translate(Instruction * inst)
{
    // 操作符
    IRInstOperator op = inst->getOp();

    translate_handler pIter;
    if (op >= IRINST_OP_MAX || (pIter = translator_handlers[op]) == nullptr) {
        // 没有找到，则说明当前不支持
        printf("Translate: Operator(%d) not support", (int) op);
        return;
    }

    // 开启时输出IR指令作为注释
    if (showLinearIR) {
        outputIRInstruction(inst);
    }

    (this->*(pIter))(inst);
}

///
/// @brief 输出IR指令
///
void InstSelectorArm64::outputIRInstruction(Instruction * inst)
{
    std::string irStr;
    inst->toString(irStr);
    if (!irStr.empty()) {
        iloc.comment(irStr);
    }
}

/// @brief NOP翻译成ARM64汇编
/// @param inst IR指令
void InstSelectorArm64::translate_nop(Instruction * inst)
{
    (void) inst;
    iloc.nop();
}

/// @brief Label指令指令翻译成ARM64汇编
/// @param inst IR指令
void InstSelectorArm64::translate_label(Instruction * inst)
{
    Instanceof(labelInst, LabelInstruction *, inst);

    ArmInst * ai = iloc.getCode().back();
    if (ai->opcode[0] == 'b' && ai->result == labelInst->getName())
        ai->setDead();
    iloc.label(labelInst->getName());
}

/// @brief goto指令指令翻译成ARM64汇编
/// @param inst IR指令
void InstSelectorArm64::translate_goto(Instruction * inst)
{
    Instanceof(gotoInst, GotoInstruction *, inst);
    Value * v = gotoInst->getCondiValue();

    if (v) {
        // 条件分支
        if (lstcmp != IRINST_OP_MAX) {
            iloc.branch(CSTR(lstcmp), gotoInst->iftrue->getName());
            iloc.jump(gotoInst->iffalse->getName());
            lstcmp = IRINST_OP_MAX;
        } else {
            iloc.branch("ne", gotoInst->iftrue->getName());
            iloc.jump(gotoInst->iffalse->getName());
        }
    } else
        // 无条件跳转
        iloc.jump(gotoInst->iftrue->getName());
}

/// @brief 函数入口指令翻译成ARM64汇编
/// @param inst IR指令
void InstSelectorArm64::translate_entry(Instruction * inst)
{
    // 查看保护的寄存器
    auto & protectedReg = func->getProtectedReg();

    int i = 0, m = protectedReg.size() - 1;
    while (i < m) {
        int32_t xa = protectedReg[i], xb = protectedReg[i + 1];
        i += 2;
        iloc.inst("stp", "x" + std::to_string(xa), "x" + std::to_string(xb), "[sp,#-16]!");
    }
    if (i <= m)
        iloc.inst("str", "x" + std::to_string(protectedReg[i]), "[sp,#-16]!");

    // 为fun分配栈帧，含局部变量、函数调用值传递的空间等
    iloc.allocStack(func, ARM64_TMP_REG_NO);
}

/// @brief 函数出口指令翻译成ARM64汇编
/// @param inst IR指令
void InstSelectorArm64::translate_exit(Instruction * inst)
{
    if (inst->getOperandsNum()) {
        // 存在返回值
        Value * retVal = inst->getOperand(0);

        // 赋值给寄存器R0
        iloc.load_var(0, retVal);
    }

    // 恢复栈空间
    int32_t dp = func->getMaxDep();
    if (dp)
        iloc.inst("add", "sp", "sp", iloc.toStr(dp));

    auto & protectedReg = func->getProtectedReg();
    if (!protectedReg.empty()) {
        int m = protectedReg.size();
        if (m & 1)
            iloc.inst("ldr", "x" + std::to_string(protectedReg[m - 1]), "[sp],#16");
        int i = (m - 2) | 1;
        while (i > 0) {
            int32_t xa = protectedReg[i - 1], xb = protectedReg[i];
            i -= 2;
            iloc.inst("ldp", "x" + std::to_string(xa), "x" + std::to_string(xb), "[sp],#16");
        }
    }

    iloc.inst("ret", "");
}

/// @brief 赋值指令翻译成ARM64汇编
/// @param inst IR指令
void InstSelectorArm64::translate_assign(Instruction * inst)
{
    Value * result = inst->getOperand(0);
    Value * arg1 = inst->getOperand(1);

    int32_t arg1_regId = arg1->getRegId();
    int32_t result_regId = result->getRegId();

    if (arg1_regId != -1) {
        // 寄存器 => 内存
        // 寄存器 => 寄存器

        // r8 -> rs 可能用到r9
        iloc.store_var(arg1_regId, result, ARM64_TMP_REG_NO);
    } else if (result_regId != -1) {
        // 内存变量 => 寄存器

        iloc.load_var(result_regId, arg1);
    } else {
        // 内存变量 => 内存变量

        int32_t temp_regno = simpleRegisterAllocator.Allocate();

        // arg1 -> r8
        iloc.load_var(temp_regno, arg1);

        // r8 -> rs 可能用到r9
        iloc.store_var(temp_regno, result, ARM64_TMP_REG_NO);

        simpleRegisterAllocator.free(temp_regno);
    }
}

/// @brief 二元操作指令翻译成ARM64汇编
/// @param inst IR指令
/// @param operator_name 操作码
/// @param rs_reg_no 结果寄存器号
/// @param op1_reg_no 源操作数1寄存器号
/// @param op2_reg_no 源操作数2寄存器号
void InstSelectorArm64::translate_two_operator(Instruction * inst, string operator_name)
{
    Value * result = inst;
    Value * arg1 = inst->getOperand(0);
    Value * arg2 = inst->getOperand(1);

    int32_t arg1_reg_no = arg1->getRegId();
    int32_t arg2_reg_no = arg2->getRegId();
    int32_t result_reg_no = inst->getRegId();
    int32_t load_result_reg_no, load_arg1_reg_no, load_arg2_reg_no;

    // 看arg1是否是寄存器，若是则寄存器寻址，否则要load变量到寄存器中
    if (arg1_reg_no == -1) {

        // 分配一个寄存器r8
        load_arg1_reg_no = ARM64_TMP_REG_NO;

        // arg1 -> r8，这里可能由于偏移不满足指令的要求，需要额外分配寄存器
        iloc.load_var(load_arg1_reg_no, arg1);
    } else {
        load_arg1_reg_no = arg1_reg_no;
    }

    // 看arg2是否是寄存器，若是则寄存器寻址，否则要load变量到寄存器中
    if (arg2_reg_no == -1) {

        // 分配一个寄存器r9
        load_arg2_reg_no = ARM64_TMP_REG_NO2;

        // arg2 -> r9
        iloc.load_var(load_arg2_reg_no, arg2);
    } else {
        load_arg2_reg_no = arg2_reg_no;
    }

    // 看结果变量是否是寄存器，若不是则需要分配一个新的寄存器来保存运算的结果
    if (result_reg_no == -1) {
        // 分配一个寄存器r10，用于暂存结果
        load_result_reg_no = ARM64_TMP_REG_NO2;
    } else {
        load_result_reg_no = result_reg_no;
    }

    // r8 + r9 -> r10
    iloc.inst(operator_name,
              PlatformArm64::regName[load_result_reg_no],
              PlatformArm64::regName[load_arg1_reg_no],
              PlatformArm64::regName[load_arg2_reg_no]);

    // 结果不是寄存器，则需要把rs_reg_name保存到结果变量中
    if (result_reg_no == -1) {

        // 这里使用预留的临时寄存器，因为立即数可能过大，必须借助寄存器才可操作。

        // r10 -> result
        iloc.store_var(load_result_reg_no, result, ARM64_TMP_REG_NO);
    }
}

/// @brief 整数加法指令翻译成ARM64汇编
/// @param inst IR指令
void InstSelectorArm64::translate_add_int32(Instruction * inst)
{
    translate_two_operator(inst, "add");
}

/// @brief 整数减法指令翻译成ARM64汇编
/// @param inst IR指令
void InstSelectorArm64::translate_sub_int32(Instruction * inst)
{
    translate_two_operator(inst, "sub");
}

void InstSelectorArm64::translate_mul_int32(Instruction * inst)
{
    translate_two_operator(inst, "mul");
}

void InstSelectorArm64::translate_div_int32(Instruction * inst)
{
    translate_two_operator(inst, "sdiv");
}

void InstSelectorArm64::translate_fadd(Instruction *inst) {
    translate_two_operator(inst, "fadd");
}

void InstSelectorArm64::translate_fsub(Instruction *inst) {
    translate_two_operator(inst, "fsub");
}

void InstSelectorArm64::translate_fmul(Instruction *inst) {
    translate_two_operator(inst, "fmul");
}

void InstSelectorArm64::translate_fdiv(Instruction *inst) {
    translate_two_operator(inst, "fdiv");
}

void InstSelectorArm64::translate_fmod(Instruction *inst) {
    translate_two_operator(inst, "fmod");
}

void InstSelectorArm64::translate_rem_int32(Instruction * inst)
{
    // TODO 计算降级：当op2为整数常量，op2=2^n时，改为and rs,ra,op-1
    Value * arg1 = inst->getOperand(0);
    Value * arg2 = inst->getOperand(1);
    int32_t reg1 = arg1->getRegId();
    int32_t reg2 = arg2->getRegId();
    int32_t res = inst->getRegId();

    if (res != -1) {
        // 参数寄存器与结果寄存器相同时，要借助临时寄存器
        if (res == reg1) {
            iloc.inst("mov", PlatformArm64::regName[ARM64_TMP_REG_NO], PlatformArm64::regName[reg1]);
            inst->getOperand(0)->setRegId(ARM64_TMP_REG_NO);
        } else if (res == reg2) {
            iloc.inst("mov", PlatformArm64::regName[ARM64_TMP_REG_NO], PlatformArm64::regName[reg2]);
            inst->getOperand(1)->setRegId(ARM64_TMP_REG_NO);
        }
    }
    translate_two_operator(inst, "rem");
    arg1->setRegId(reg1);
    arg2->setRegId(reg2);
}

void InstSelectorArm64::translate_gep(Instruction *inst) {
    Value *arg1 = inst->getOperand(0);
    Value *arg2 = inst->getOperand(1);

    int32_t baseReg = -1;
    int64_t baseOff;
    arg1->getMemoryAddr(&baseReg, &baseOff);

    Instanceof(off, ConstInt*, arg2);
    uint32_t l = ((ArrayType*)(inst->getType()))->getElementType()->getSize();
    if (off) {
        // if (baseReg == null) store baseReg;
        inst->setMemoryAddr(baseReg, baseOff + off->getVal() * l);
    } else {
        // TODO
        // %t1 = getelemptr [3xi32] %l0, %l1
        // // mul l1, l1, lx
        if (baseReg == -1) {
            baseReg = ARM64_TMP_REG_NO;
            iloc.load_var(baseReg, arg1);
        }
        int32_t reg2 = arg2->getRegId();
        if (reg2 == -1) {
            reg2 = ARM64_TMP_REG_NO2;
            iloc.load_var(reg2, arg2);
        }
        if (__builtin_popcount(l) == 1) {
            iloc.inst("add", "x"+to_string(ARM64_TMP_REG_NO2), "x"+to_string(baseReg), "x"+to_string(reg2)+",lsl "+to_string(__builtin_ctz(l)));
        } else {
            iloc.inst("mov", "x"+to_string(ARM64_TMP_REG_NO), "#"+to_string(l));
            iloc.inst("madd", "x"+to_string(ARM64_TMP_REG_NO2),
                        "x"+to_string(reg2),
                        "x"+to_string(ARM64_TMP_REG_NO)
                        +",x"+to_string(baseReg));
        }
        inst->setMemoryAddr(ARM64_TMP_REG_NO2, baseOff);
        // add t1, l0, l1, lsl 2
    }
}

void InstSelectorArm64::translate_store(Instruction *inst) {
    int64_t off = 0;
    Value *ptr = inst->getOperand(0),
          *src = inst->getOperand(1);
    int32_t basereg = ptr->getRegId(),
            loadreg = src->getRegId();
    if (loadreg == -1) {
        loadreg = ARM64_TMP_REG_NO;
        iloc.load_var(loadreg, src);
    }
    if (basereg == -1) {
        ptr->getMemoryAddr(&basereg, &off);
    }
    
    iloc.store_base(loadreg, basereg, off, ARM64_TMP_REG_NO);
}

void InstSelectorArm64::translate_load(Instruction *inst) {
    int64_t off = 0;
    Value *addr = inst->getOperand(0);
    int32_t basereg = addr->getRegId(),
            loadreg = inst->getRegId();
    if (loadreg == -1) {
        loadreg = ARM64_TMP_REG_NO;
        iloc.load_var(loadreg, addr);
    }
    if (basereg == -1) {
        addr->getMemoryAddr(&basereg, &off);
    }
    iloc.load_base(loadreg, basereg, off);
}

void InstSelectorArm64::translate_bi_op(Instruction * inst)
{
    // const char *op;
    switch (inst->getOp()) {
        case IRINST_OP_IEQ:
        case IRINST_OP_INE:
        case IRINST_OP_IGT:
        case IRINST_OP_ILE:
        case IRINST_OP_IGE:
        case IRINST_OP_ILT: {
            lstcmp = inst->getOp();
            Instanceof(v, ConstInt *, inst->getOperand(1));
            if (v && v->getVal() == 0) {
                ArmInst * it = iloc.getCode().back();
                int32_t reg = inst->getOperand(0)->getRegId();
                if (reg >= 0 && PlatformArm64::regName[reg] == it->arg1 &&
                    (it->opcode == "add" || it->opcode == "sub")) {
                    it->opcode += "s";
                    break;
                }
            }
            int32_t x = inst->getRegId();
            inst->setRegId(ARM64_ZR_REG_NO);
            translate_two_operator(inst, "subs");
            inst->setRegId(x);
        }
        default:
            return;
    }
}

void InstSelectorArm64::translate_cast(Instruction * inst)
{
    Instanceof(cast, CastInstruction *, inst);
    Value * arg = inst->getOperand(0);
    int32_t reg = arg->getRegId();
    switch (cast->getCastType()) {
        case CastInstruction::BOOL_TO_INT:
            if (reg == -1) {
                iloc.load_var(ARM64_TMP_REG_NO, arg);
                reg = ARM64_TMP_REG_NO2;
            }
            iloc.inst("cset", PlatformArm64::regName[reg], CSTR(lstcmp));
            if (reg == ARM64_TMP_REG_NO2) {
                iloc.store_var(ARM64_TMP_REG_NO2, arg, ARM64_TMP_REG_NO);
            }
            break;
        default:
            break;
    }
}

void InstSelectorArm64::translate_xor_int32(Instruction * inst)
{
    Instanceof(l, Instruction *, inst->getOperand(0));
    Instanceof(v, ConstInt *, inst->getOperand(1));
    IRInstOperator ir;
    if (v && l && v->getVal() == 1 && (ir = l->getOp()) >= IRINST_OP_IEQ && ir <= IRINST_OP_ILT) {
        int32_t regId = inst->getRegId(), load_regId;
        if (regId == -1) {
            load_regId = simpleRegisterAllocator.Allocate(inst);
        } else {
            load_regId = regId;
        }
        iloc.inst("cset", PlatformArm64::regName[load_regId], CSTRJ(ir));
        if (regId == -1) {
            iloc.store_var(load_regId, inst, ARM64_FP_REG_NO);
        }
        simpleRegisterAllocator.free(inst);
        return;
    }
    translate_two_operator(inst, "eor");
}

/// @brief 函数调用指令翻译成ARM64汇编
/// @param inst IR指令
void InstSelectorArm64::translate_call(Instruction * inst)
{
    FuncCallInstruction * callInst = dynamic_cast<FuncCallInstruction *>(inst);

    int32_t operandNum = callInst->calledFunction->getParams().size();

    if (operandNum != realArgCount) {

        // 两者不一致 也可能没有ARG指令，正常
        if (realArgCount != 0) {

            minic_log(LOG_ERROR, "ARG指令的个数与调用函数个数不一致");
        }
    }

    if (operandNum) {

        // 强制占用这几个寄存器参数传递的寄存器
        simpleRegisterAllocator.Allocate(0);
        simpleRegisterAllocator.Allocate(1);
        simpleRegisterAllocator.Allocate(2);
        simpleRegisterAllocator.Allocate(3);
        simpleRegisterAllocator.Allocate(4);
        simpleRegisterAllocator.Allocate(5);
        simpleRegisterAllocator.Allocate(6);
        simpleRegisterAllocator.Allocate(7);

        // 前四个的后面参数采用栈传递
        int esp = 0;
        for (int32_t k = 8; k < operandNum; k++) {

            auto arg = callInst->getOperand(k);

            // 新建一个内存变量，用于栈传值到形参变量中
            MemVariable * newVal = func->newMemVariable((Type *) PointerType::get(arg->getType()));
            newVal->setMemoryAddr(ARM64_SP_REG_NO, esp);
            esp += 4;

            Instruction * assignInst = new MoveInstruction(func, newVal, arg);

            // 翻译赋值指令
            translate_assign(assignInst);

            delete assignInst;
        }

        for (int32_t k = 0, d = 0; k < operandNum && k < 8; k++) {

            auto arg = callInst->getOperand(k);
            if (arg == callInst) {
                continue;
            }

            // 检查实参的类型是否是临时变量。
            // 如果是临时变量，该变量可更改为寄存器变量即可，或者设置寄存器号
            // 如果不是，则必须开辟一个寄存器变量，然后赋值即可

            Instruction * assignInst = new MoveInstruction(func, PlatformArm64::intRegVal[d], arg);

            // 翻译赋值指令
            translate_assign(assignInst);

            delete assignInst;
            d++;
        }
    }

    iloc.call_fun(callInst->getName());

    if (operandNum) {
        simpleRegisterAllocator.free(0);
        simpleRegisterAllocator.free(1);
        simpleRegisterAllocator.free(2);
        simpleRegisterAllocator.free(3);
        simpleRegisterAllocator.free(4);
        simpleRegisterAllocator.free(5);
        simpleRegisterAllocator.free(6);
        simpleRegisterAllocator.free(7);
    }
    // 函数调用后清零，使得下次可正常统计
    realArgCount = 0;
}

///
/// @brief 实参指令翻译成ARM64汇编
/// @param inst
///
void InstSelectorArm64::translate_arg(Instruction * inst)
{
    // 翻译之前必须确保源操作数要么是寄存器，要么是内存，否则出错。
    Value * src = inst->getOperand(0);

    // 当前统计的ARG指令个数
    int32_t regId = src->getRegId();

    if (realArgCount < 8) {
        // 前四个参数
        if (regId != -1) {
            if (regId != realArgCount) {
                // 肯定寄存器分配有误
                minic_log(LOG_ERROR, "第%d个ARG指令对象寄存器分配有误: %d", argCount + 1, regId);
            }
        } else {
            minic_log(LOG_ERROR, "第%d个ARG指令对象不是寄存器", argCount + 1);
        }
    } else {
        // 必须是内存分配，若不是则出错
        int32_t baseRegId;
        bool result = src->getMemoryAddr(&baseRegId);
        if ((!result) || (baseRegId != ARM64_SP_REG_NO)) {

            minic_log(LOG_ERROR, "第%d个ARG指令对象不是SP寄存器寻址", argCount + 1);
        }
    }

    realArgCount++;
}
