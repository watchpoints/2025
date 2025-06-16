﻿///
/// @file Instruction.h
/// @brief IR指令头文件
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
#pragma once

#include "User.h"

class Function;

#define IROP(OP) IRInstOperator::IRINST_OP_##OP

/// @brief IR指令操作码
typedef enum IRInstOperator {

    /// @brief 函数入口指令，对应函数的prologue，用户栈空间分配、寄存器保护等
    IRINST_OP_ENTRY,

    /// @brief 函数出口指令，对应函数的epilogue，用于栈空间的恢复与清理、寄存器恢复等
    IRINST_OP_EXIT,

    /// @brief Label指令，用于语句的跳转
    IRINST_OP_LABEL,

    /// @brief 无条件分支指令
    IRINST_OP_GOTO,

    /// @brief 整数的加法指令，二元运算
    IRINST_OP_IADD,

    /// @brief 整数的减法指令，二元运算
    IRINST_OP_ISUB,

    IRINST_OP_IMUL,

    IRINST_OP_IDIV,

    IRINST_OP_IMOD,

    IRINST_OP_IEQ,

    IRINST_OP_INE,

    IRINST_OP_IGT,

    IRINST_OP_ILE,

    IRINST_OP_IGE,

    IRINST_OP_ILT,

    /// @brief 整数的加法指令，二元运算
    IRINST_OP_FADD,

    /// @brief 整数的减法指令，二元运算
    IRINST_OP_FSUB,

    IRINST_OP_FMUL,

    IRINST_OP_FDIV,

    IRINST_OP_FMOD,

    IRINST_OP_FEQ,

    IRINST_OP_FNE,

    IRINST_OP_FGT,

    IRINST_OP_FGE,

    IRINST_OP_FLT,

    IRINST_OP_FLE,

    IRINST_OP_XOR,

    /// @brief 赋值指令，一元运算
    IRINST_OP_ASSIGN,

    /// @brief 类型转换指令
    IRINST_OP_CAST,

    /// @brief 函数调用，多目运算，个数不限
    IRINST_OP_FUNC_CALL,

    /// @brief 实参ARG指令，单目运算
    IRINST_OP_ARG,

    /// @brief 数组元素访问指令
    IRINST_OP_GEP,

    /// @brief 存储指令
    IRINST_OP_STORE,

    /// @brief 载入指令
    IRINST_OP_LOAD,

    /* 后续可追加其他的IR指令 */

    /// @brief 最大指令码，也是无效指令
    IRINST_OP_MAX
} IRInstOperator;

///
/// @brief IR指令的基类, 指令自带值，也就是常说的临时变量
///
class Instruction : public User {

public:
    /// @brief 构造函数
    /// @param op
    /// @param result
    explicit Instruction(Function * _func, IRInstOperator op, Type * _type);

    /// @brief 析构函数
    virtual ~Instruction() = default;

    /// @brief 获取指令操作码
    /// @return 指令操作码
    IRInstOperator getOp();

    ///
    /// @brief 转换成IR指令文本形式
    /// @param str IR指令文本
    ///
    virtual void toString(std::string & str);

    /// @brief 是否是Dead指令
    bool isDead();

    /// @brief 设置指令是否是Dead指令
    /// @param _dead 是否是Dead指令，true：Dead, false: 非Dead
    void setDead(bool _dead = true);

    ///
    /// @brief 获取当前指令所在函数
    /// @return Function* 函数对象
    ///
    Function * getFunction();

    ///
    /// @brief 检查指令是否有值
    /// @return true
    /// @return false
    ///
    bool hasResultValue();

    ///
    /// @brief 获得分配的寄存器编号或ID
    /// @return int32_t 寄存器编号
    ///
    int32_t getRegId() override
    {
        return regId;
    }

    void setRegId(int32_t reg) override
    {
        regId = reg;
    }

    ///
    /// @brief @brief 如是内存变量型Value，则获取基址寄存器和偏移
    /// @param regId 寄存器编号
    /// @param offset 相对偏移
    /// @return true 是内存型变量
    /// @return false 不是内存型变量
    ///
    bool getMemoryAddr(int32_t * _regId = nullptr, int64_t * _offset = nullptr) override
    {
        // 内存寻址时，必须要指定基址寄存器

        // 没有指定基址寄存器则返回false
        if (this->baseRegNo == -1) {
            return false;
        }

        // 设置基址寄存器
        if (_regId) {
            *_regId = this->baseRegNo;
        }

        // 设置偏移
        if (_offset) {
            *_offset = this->offset;
        }

        return true;
    }

    ///
    /// @brief 设置内存寻址的基址寄存器和偏移
    /// @param _regId 基址寄存器编号
    /// @param _offset 偏移
    ///
    void setMemoryAddr(int32_t _regId, int64_t _offset) override;

    ///
    /// @brief 对该Value进行Load用的寄存器编号
    /// @return int32_t 寄存器编号
    ///
    int32_t getLoadRegId() override
    {
        return this->loadRegNo;
    }

    ///
    /// @brief 对该Value进行Load用的寄存器编号
    /// @return int32_t 寄存器编号
    ///
    void setLoadRegId(int32_t regId) override
    {
        this->loadRegNo = regId;
    }

protected:
    ///
    /// @brief IR指令操作码
    ///
    enum IRInstOperator op = IRINST_OP_MAX;

    ///
    /// @brief 是否是Dead指令
    ///
    bool dead = false;

    ///
    /// @brief 当前指令属于哪个函数
    ///
    Function * func = nullptr;

    ///
    /// @brief 寄存器编号，-1表示没有分配寄存器，大于等于0代表是寄存器型Value
    ///
    int32_t regId = -1;

    ///
    /// @brief 变量在栈内的偏移量，对于全局变量默认为0，临时变量没有意义
    ///
    int32_t offset = 0;

    ///
    /// @brief 栈内寻找时基址寄存器编号
    ///
    int32_t baseRegNo = -1;

    ///
    /// @brief 栈内寻找时基址寄存器名字
    ///
    std::string baseRegName;

    ///
    /// @brief 变量加载到寄存器中时对应的寄存器编号
    ///
    int32_t loadRegNo = -1;
};
