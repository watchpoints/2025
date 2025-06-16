﻿///
/// @file IRGenerator.cpp
/// @brief AST遍历产生线性IR的源文件
/// @author zenglj (zenglj@live.com)
/// @version 1.1
/// @date 2024-11-23
///
/// @copyright Copyright (c) 2024
///
/// @par 修改日志:
/// <table>
/// <tr><th>Date       <th>Version <th>Author  <th>Description
/// <tr><td>2024-09-29 <td>1.0     <td>zenglj  <td>新建
/// <tr><td>2024-11-23 <td>1.1     <td>zenglj  <td>表达式版增强
/// </table>
///
// #include <algorithm>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include "AST.h"
#include "Common.h"
#include "Function.h"
#include "IRCode.h"
#include "IRGenerator.h"
#include "Module.h"
#include "EntryInstruction.h"
#include "LabelInstruction.h"
#include "ExitInstruction.h"
#include "FuncCallInstruction.h"
#include "BinaryInstruction.h"
#include "MoveInstruction.h"
#include "StoreInstruction.h"
#include "LoadInstruction.h"
#include "GotoInstruction.h"
#include "TypeSystem.h"
#include "CastInstruction.h"
#include "ConstFloat.h"
#include "FloatType.h"

#define CHECK_NODE(name, son)                                                                                          \
    name = ir_visit_ast_node(son);                                                                                     \
    if (!name) {                                                                                                       \
        fprintf(stdout, "IR解析错误:%s:%d\n", __FUNCTION__, __LINE__);                                                 \
        return (false);                                                                                                \
    }

extern "C" {
static inline IRInstOperator irtype(ast_operator_type type);
static inline void backPatch(std::vector<LabelInstruction **> * a, LabelInstruction * b)
{
    for (auto i: *a) {
        *i = b;
    }
}
static inline std::vector<LabelInstruction **> * merge(std::vector<LabelInstruction **> *,
                                                       std::vector<LabelInstruction **> *);
}

/// @brief 构造函数
/// @param _root AST的根
/// @param _module 符号表
IRGenerator::IRGenerator(ast_node * _root, Module * _module) : root(_root), module(_module)
{
    /* 叶子节点 */
    ast2ir_handlers[ASTOP(LEAF_LITERAL_INT)] = &IRGenerator::ir_leaf_node_uint;
    ast2ir_handlers[ASTOP(LEAF_LITERAL_FLOAT)] = &IRGenerator::ir_leaf_node_float;
    ast2ir_handlers[ASTOP(VAR_ID)] = &IRGenerator::ir_node_var_id;
    ast2ir_handlers[ASTOP(LEAF_TYPE)] = &IRGenerator::ir_leaf_node_type;

    /* 表达式运算， 加减 */
    ast2ir_handlers[ast_operator_type::AST_OP_SUB] = &IRGenerator::ir_binary;
    ast2ir_handlers[ast_operator_type::AST_OP_ADD] = &IRGenerator::ir_binary;
    ast2ir_handlers[ast_operator_type::AST_OP_MUL] = &IRGenerator::ir_binary;
    ast2ir_handlers[ast_operator_type::AST_OP_DIV] = &IRGenerator::ir_binary;
    ast2ir_handlers[ast_operator_type::AST_OP_MOD] = &IRGenerator::ir_binary;
    ast2ir_handlers[ASTOP(EQ)] = &IRGenerator::ir_relop;
    ast2ir_handlers[ASTOP(NE)] = &IRGenerator::ir_relop;
    ast2ir_handlers[ASTOP(GT)] = &IRGenerator::ir_relop;
    ast2ir_handlers[ASTOP(GE)] = &IRGenerator::ir_relop;
    ast2ir_handlers[ASTOP(LT)] = &IRGenerator::ir_relop;
    ast2ir_handlers[ASTOP(LE)] = &IRGenerator::ir_relop;

    ast2ir_handlers[ASTOP(LOR)] = &IRGenerator::ir_or;
    ast2ir_handlers[ASTOP(LAND)] = &IRGenerator::ir_and;
    ast2ir_handlers[ASTOP(NOT)] = &IRGenerator::ir_not;

    /* 数组访问 */
    ast2ir_handlers[ASTOP(ARRAY_ACCESS)] = &IRGenerator::ir_array_access;

    /* 多维数组相关 */
    ast2ir_handlers[ASTOP(ARRAY_INIT)] = &IRGenerator::ir_array_init;

    ast2ir_handlers[ASTOP(L2R)] = &IRGenerator::ir_lval_to_r;

    ast2ir_handlers[ASTOP(BREAK)] = &IRGenerator::ir_jump;
    ast2ir_handlers[ASTOP(CONTINUE)] = &IRGenerator::ir_jump;

    /* 语句 */
    ast2ir_handlers[ast_operator_type::AST_OP_ASSIGN] = &IRGenerator::ir_assign;
    ast2ir_handlers[ast_operator_type::AST_OP_RETURN] = &IRGenerator::ir_return;

    /* 函数调用 */
    ast2ir_handlers[ast_operator_type::AST_OP_FUNC_CALL] = &IRGenerator::ir_function_call;

    /* 函数定义 */
    ast2ir_handlers[ast_operator_type::AST_OP_FUNC_DEF] = &IRGenerator::ir_function_define;
    ast2ir_handlers[ast_operator_type::AST_OP_FUNC_FORMAL_PARAMS] = &IRGenerator::ir_function_formal_params;

    /* 变量定义语句 */
    ast2ir_handlers[ast_operator_type::AST_OP_VAR_DECL] = &IRGenerator::ir_variable_declare;

    /* 语句块 */
    ast2ir_handlers[ast_operator_type::AST_OP_BLOCK] = &IRGenerator::ir_block;

    /* 编译单元 */
    ast2ir_handlers[ast_operator_type::AST_OP_COMPILE_UNIT] = &IRGenerator::ir_compile_unit;

    ast2ir_handlers[ast_operator_type::AST_OP_IF] = &IRGenerator::ir_branch;
    ast2ir_handlers[ast_operator_type::AST_OP_WHILE] = &IRGenerator::ir_loop;
    ast2ir_handlers[ASTOP(DOWHILE)] = &IRGenerator::ir_loop;

    ast2ir_handlers[ASTOP(NULL_STMT)] = &IRGenerator::ir_default;

    SLIST_INIT(&labs);
}

/// @brief 遍历抽象语法树产生线性IR，保存到IRCode中
/// @param root 抽象语法树
/// @param IRCode 线性IR
/// @return true: 成功 false: 失败
bool IRGenerator::run()
{
    ast_node * node;

    // 从根节点进行遍历
    node = ir_visit_ast_node(root);

    return node != nullptr;
}

/// @brief 根据AST的节点运算符查找对应的翻译函数并执行翻译动作
/// @param node AST节点
/// @return 成功返回node节点，否则返回nullptr
ast_node * IRGenerator::ir_visit_ast_node(ast_node * node)
{
    // 空节点
    if (nullptr == node) {
        return nullptr;
    }

    bool result;

    std::unordered_map<ast_operator_type, ast2ir_handler_t>::const_iterator pIter;
    pIter = ast2ir_handlers.find(node->node_type);
    if (pIter == ast2ir_handlers.end()) {
        // 没有找到，则说明当前不支持
        result = (this->ir_default)(node);
    } else {
        result = (this->*(pIter->second))(node);
    }

    if (!result) {
        // 语义解析错误，则出错返回
        node = nullptr;
    }

    return node;
}

/// @brief 未知节点类型的节点处理
/// @param node AST节点
/// @return 翻译是否成功，true：成功，false：失败
bool IRGenerator::ir_default(ast_node * node)
{
    // 未知的节点
    if (node->node_type != ASTOP(NULL_STMT)) {
        printf("Unkown node(%d) at line %lld\n", (int) node->node_type, (long long)node->line_no);
        if (!node->name.empty()) {
            printf("Node name: %s\n", node->name.c_str());
        }
        printf("Node has %zu children\n", node->sons.size());
    }
    return true;
}

/// @brief 编译单元AST节点翻译成线性中间IR
/// @param node AST节点
/// @return 翻译是否成功，true：成功，false：失败
bool IRGenerator::ir_compile_unit(ast_node * node)
{
    module->setCurrentFunction(nullptr);

    for (auto son: node->sons) {

        // 遍历编译单元，要么是函数定义，要么是语句
        ast_node * CHECK_NODE(son_node, son);
    }

    return true;
}

/// @brief 函数定义AST节点翻译成线性中间IR
/// @param node AST节点
/// @return 翻译是否成功，true：成功，false：失败
bool IRGenerator::ir_function_define(ast_node * node)
{
    bool result;

    // 创建一个函数，用于当前函数处理
    if (module->getCurrentFunction()) {
        // 函数中嵌套定义函数，这是不允许的，错误退出
        // TODO 自行追加语义错误处理
        return false;
    }

    // 函数定义的AST包含四个孩子
    // 第一个孩子：函数返回类型
    // 第二个孩子：函数名字
    // 第三个孩子：形参列表
    // 第四个孩子：函数体即block
    ast_node * type_node = node->sons[0];
    ast_node * name_node = node->sons[1];
    ast_node * param_node = node->sons[2];
    ast_node * block_node = node->sons[3];

    // 创建一个新的函数定义，函数的返回类型设置为VOID，待定，必须等return时才能确定，目前可以是VOID或者INT类型
    // 请注意这个与C语言的函数定义不同。在实现MiniC编译器时必须调整
    Function * newFunc = module->newFunction(name_node->name, type_node->type);
    if (!newFunc) {
        // 新定义的函数已经存在，则失败返回。
        // TODO 自行追加语义错误处理
        minic_log(LOG_ERROR, "函数重复定义：%s", name_node->name.c_str());
        return false;
    }

    // 当前函数设置有效，变更为当前的函数
    module->setCurrentFunction(newFunc);

    // 进入函数的作用域
    module->enterScope();

    // 获取函数的IR代码列表，用于后面追加指令用，注意这里用的是引用传值
    InterCode & irCode = newFunc->getInterCode();

    // 这里也可增加一个函数入口Label指令，便于后续基本块划分

    // 创建并加入Entry入口指令
    irCode.addInst(new EntryInstruction(newFunc));

    // 创建出口指令并不加入出口指令，等函数内的指令处理完毕后加入出口指令
    LabelInstruction * exitLabelInst = new LabelInstruction(newFunc);

    // 函数出口指令保存到函数信息中，因为在语义分析函数体时return语句需要跳转到函数尾部，需要这个label指令
    newFunc->setExitLabel(exitLabelInst);

    // 遍历形参，没有IR指令，不需要追加
    result = ir_function_formal_params(param_node);
    if (!result) {
        // 形参解析失败
        // TODO 自行追加语义错误处理
        fputs("形参解析失败\n", stderr);
        return false;
    }
    node->blockInsts.addInst(param_node->blockInsts);

    // 新建一个Value，用于保存函数的返回值，如果没有返回值可不用申请
    LocalVariable * retValue = nullptr;
    if (!type_node->type->isVoidType()) {

        // 保存函数返回值变量到函数信息中，在return语句翻译时需要设置值到这个变量中
        retValue = static_cast<LocalVariable *>(module->newVarValue(type_node->type));
    }
    newFunc->setReturnValue(retValue);

    // 函数内已经进入作用域，内部不再需要做变量的作用域管理
    block_node->needScope = false;

    // 遍历block
    result = ir_block(block_node);
    if (!result) {
        // block解析失败
        // TODO 自行追加语义错误处理
        return false;
    }

    // IR指令追加到当前的节点中
    node->blockInsts.addInst(block_node->blockInsts);

    // 此时，所有指令都加入到当前函数中，也就是node->blockInsts

    // node节点的指令移动到函数的IR指令列表中
    irCode.addInst(node->blockInsts);

    // 添加函数出口Label指令，主要用于return语句跳转到这里进行函数的退出
    irCode.addInst(exitLabelInst);

    // 函数出口指令
    irCode.addInst(new ExitInstruction(newFunc, retValue));

    // 恢复成外部函数
    module->setCurrentFunction(nullptr);

    // 退出函数的作用域
    module->leaveScope();

    return true;
}

/// @brief 形式参数AST节点翻译成线性中间IR
/// @param node AST节点
/// @return 翻译是否成功，true：成功，false：失败
bool IRGenerator::ir_function_formal_params(ast_node * node)
{
    // TODO 目前形参还不支持，直接返回true

    // 每个形参变量都创建对应的临时变量，用于表达实参转递的值
    // 而真实的形参则创建函数内的局部变量。
    // 然后产生赋值指令，用于把表达实参值的临时变量拷贝到形参局部变量上。
    // 请注意这些指令要放在Entry指令后面，因此处理的先后上要注意。
    for (auto i: node->sons) {
        module->newFuncParam(i->type, i->name);
    }

    return true;
}

/// @brief 函数调用AST节点翻译成线性中间IR
/// @param node AST节点
/// @return 翻译是否成功，true：成功，false：失败
bool IRGenerator::ir_function_call(ast_node * node)
{
    std::vector<Value *> realParams;

    // 获取当前正在处理的函数
    Function * currentFunc = module->getCurrentFunction();

    // 函数调用的节点包含两个节点：
    // 第一个节点：函数名节点
    // 第二个节点：实参列表节点

    const std::string & funcName = node->sons[0]->name;
    int64_t lineno = node->sons[0]->line_no;

    // 根据函数名查找函数，看是否存在。若不存在则出错
    // 这里约定函数必须先定义后使用
    auto calledFunction = module->findFunction(funcName);
    if (nullptr == calledFunction) {
        minic_log(LOG_ERROR, "函数(%s)未定义或声明", funcName.c_str());
        return false;
    }

    // 当前函数存在函数调用
    currentFunc->setExistFuncCall(true);

    // 如果没有孩子，也认为是没有参数
    if (node->sons.size() > 1) {
        ast_node * paramsNode = node->sons[1];
        if (!paramsNode->sons.empty()) {
            int32_t argsCount = (int32_t) paramsNode->sons.size();

            // 当前函数中调用函数实参个数最大值统计，实际上是统计实参传参需在栈中分配的大小
            // 因为目前的语言支持的int和float都是四字节的，只统计个数即可
            if (argsCount > currentFunc->getMaxFuncCallArgCnt()) {
                currentFunc->setMaxFuncCallArgCnt(argsCount);
            }

            // 遍历参数列表，孩子是表达式
            // 这里自左往右计算表达式
            for (auto son: paramsNode->sons) {

                // 遍历Block的每个语句，进行显示或者运算
                ast_node * CHECK_NODE(temp, son);

                realParams.push_back(temp->val);
                node->blockInsts.addInst(temp->blockInsts);
            }
        }
    }

    // TODO 这里请追加函数调用的语义错误检查，这里只进行了函数参数的个数检查等，其它请自行追加。
    if (realParams.size() != calledFunction->getParams().size()) {
        // 函数参数的个数不一致，语义错误
        minic_log(LOG_ERROR, "第%lld的函数%s参数个数有误", (long long) lineno, funcName.c_str());
        return false;
    }

    // 返回调用有返回值，则需要分配临时变量，用于保存函数调用的返回值
    Type * type = calledFunction->getReturnType();

    FuncCallInstruction * funcCallInst = new FuncCallInstruction(currentFunc, calledFunction, realParams, type);

    // 创建函数调用指令
    node->blockInsts.addInst(funcCallInst);

    // 函数调用结果Value保存到node中，可能为空，上层节点可利用这个值
    node->val = funcCallInst;

    return true;
}

/// @brief 语句块（含函数体）AST节点翻译成线性中间IR
/// @param node AST节点
/// @return 翻译是否成功，true：成功，false：失败
bool IRGenerator::ir_block(ast_node * node)
{
    // 进入作用域
    if (node->needScope) {
        module->enterScope();
    }

    std::vector<ast_node *>::iterator pIter;
    for (pIter = node->sons.begin(); pIter != node->sons.end(); ++pIter) {

        // 遍历Block的每个语句，进行显示或者运算
        ast_node * CHECK_NODE(temp, *pIter);

        node->blockInsts.addInst(temp->blockInsts);
    }

    // 离开作用域
    if (node->needScope) {
        module->leaveScope();
    }

    return true;
}

/// @brief 整数加法AST节点翻译成线性中间IR
/// @param node AST节点
/// @return 翻译是否成功，true：成功，false：失败
bool IRGenerator::ir_binary(ast_node * node)
{
    ast_node * src1_node = node->sons[0];
    ast_node * src2_node = node->sons[1];

    // 加法的左边操作数
    ast_node * CHECK_NODE(left, src1_node);

    // 加法的右边操作数
    ast_node * CHECK_NODE(right, src2_node);

    // 如果需要类型转换
    Value * leftVal = left->val;
    Value * rightVal = right->val;

    // 获取操作数类型，并确定结果类型和适合的操作符
    Type * leftType = leftVal->getType();
    Type * rightType = rightVal->getType();

    // 获取适当的操作符
    IRInstOperator op = TypeSystem::getAppropriateOp(irtype(node->node_type), leftType, rightType);

    // 获取结果类型
    Type * resultType = TypeSystem::getBinaryResultType(op, leftType, rightType);

    Function * func = module->getCurrentFunction();

    // 如果操作数类型与结果类型不同，需要进行隐式类型转换
    if (resultType->isFloatType()) {
        if (!leftType->isFloatType()) {
            // 整数转浮点 - 使用CastInstruction
            Instruction * castInst =
                new CastInstruction(func, leftVal, FloatType::getTypeFloat(), CastInstruction::INT_TO_FLOAT);
            left->blockInsts.addInst(castInst);
            leftVal = castInst;
        } else if (!rightType->isFloatType()) {
            // 整数转浮点 - 使用CastInstruction
            Instruction * castInst =
                new CastInstruction(func, rightVal, FloatType::getTypeFloat(), CastInstruction::INT_TO_FLOAT);
            right->blockInsts.addInst(castInst);
            rightVal = castInst;
        }
    }

    Instruction * addInst = new BinaryInstruction(func, op, leftVal, rightVal, resultType);

    // 创建临时变量保存IR的值，以及线性IR指令
    node->blockInsts.addInst(left->blockInsts);
    node->blockInsts.addInst(right->blockInsts);
    node->blockInsts.addInst(addInst);

    node->val = addInst;

    // 设置节点的类型
    node->type = resultType;

    return true;
}

bool IRGenerator::ir_relop(ast_node * node)
{
    bool ret = ir_binary(node);
    if (!ret)
        return false;

    ast_node * parent = node->parent;
    ast_operator_type tp;
    Function * func = module->getCurrentFunction();
    if (parent && ((tp = parent->node_type) == ASTOP(LAND) || tp == ASTOP(LOR) || tp == ASTOP(IF) ||
                   tp == ASTOP(WHILE) || tp == ASTOP(DOWHILE))) {
        auto go = new GotoInstruction(func, node->val, nullptr, nullptr);
        node->truelist = new std::vector<LabelInstruction **>{&go->iftrue};
        node->falselist = new std::vector<LabelInstruction **>{&go->iffalse};
        node->blockInsts.addInst(go);
    } else {
        auto cast = new CastInstruction(func, node->val, IntegerType::getTypeInt(), CastInstruction::BOOL_TO_INT);
        node->blockInsts.addInst(cast);
        node->val = cast;
    }

    return true;
}

/// @brief 赋值AST节点翻译成线性中间IR
/// @param node AST节点
/// @return 翻译是否成功，true：成功，false：失败
bool IRGenerator::ir_assign(ast_node * node)
{
    ast_node * son1_node = node->sons[0];
    ast_node * son2_node = node->sons[1];

    // 赋值节点，自右往左运算

    // 赋值运算符的左侧操作数
    ast_node * left = ir_visit_ast_node(son1_node);
    if (!left) {
        // 某个变量没有定值
        // 这里缺省设置变量不存在则创建，因此这里不会错误
        return false;
    }

    // 赋值运算符的右侧操作数
    ast_node * right = ir_visit_ast_node(son2_node);
    if (!right) {
        // 某个变量没有定值
        return false;
    }

    // 这里只处理整型的数据，如需支持实数，则需要针对类型进行处理
    // TODO real number add

    Instruction * movInst;
    Function * func = module->getCurrentFunction();
    if (left->node_type == ASTOP(ARRAY_ACCESS)) {
        movInst = new StoreInstruction(func, left->val, right->val);
    } else {
        movInst = new MoveInstruction(func, left->val, right->val);
    }

    // 创建临时变量保存IR的值，以及线性IR指令
    node->blockInsts.addInst(right->blockInsts);
    node->blockInsts.addInst(left->blockInsts);
    node->blockInsts.addInst(movInst);

    // 这里假定赋值的类型是一致的
    node->val = movInst;

    return true;
}

/// @brief return节点翻译成线性中间IR
/// @param node AST节点
/// @return 翻译是否成功，true：成功，false：失败
bool IRGenerator::ir_return(ast_node * node)
{
    ast_node * right = nullptr;

    // return语句可能没有没有表达式，也可能有，因此这里必须进行区分判断
    if (!node->sons.empty()) {

        ast_node * son_node = node->sons[0];

        // 返回的表达式的指令保存在right节点中
        right = ir_visit_ast_node(son_node);
        if (!right) {
            // 某个变量没有定值
            return false;
        }
    }

    // 这里只处理整型的数据，如需支持实数，则需要针对类型进行处理
    Function * currentFunc = module->getCurrentFunction();

    // 创建临时变量保存IR的值，以及线性IR指令
    node->blockInsts.addInst(right->blockInsts);

    // 返回值赋值到函数返回值变量上，然后跳转到函数的尾部
    node->blockInsts.addInst(new MoveInstruction(currentFunc, currentFunc->getReturnValue(), right->val));

    // 跳转到函数的尾部出口指令上
    node->blockInsts.addInst(new GotoInstruction(currentFunc, currentFunc->getExitLabel()));

    node->val = right->val;

    // TODO 设置类型

    return true;
}

/// @brief 类型叶子节点翻译成线性中间IR
/// @param node AST节点
/// @return 翻译是否成功，true：成功，false：失败
bool IRGenerator::ir_leaf_node_type(ast_node * node)
{
    // 不需要做什么，直接从节点中获取即可。

    return !node->type->isVoidType();
}

/// @brief 标识符叶子节点翻译成线性中间IR，变量声明的不走这个语句
/// @param node AST节点
/// @return 翻译是否成功，true：成功，false：失败
bool IRGenerator::ir_node_var_id(ast_node * node)
{
    Value * val;

    // 查找ID型Value
    // 变量，则需要在符号表中查找对应的值

    val = module->findVarValue(node->name);
    node->val = val;

    return true;
}

/// @brief 无符号整数字面量叶子节点翻译成线性中间IR
/// @param node AST节点
/// @return 翻译是否成功，true：成功，false：失败
bool IRGenerator::ir_leaf_node_uint(ast_node * node)
{
    ConstInt * val;

    // 新建一个整数常量Value
    val = module->newConstInt(node->integer_val);

    node->val = val;

    return true;
}

/// @brief float数字面量叶子节点翻译成线性中间IR
/// @param node AST节点
/// @return 翻译是否成功，true：成功，false：失败
bool IRGenerator::ir_leaf_node_float(ast_node * node)
{
    // 创建浮点常量Value
    ConstFloat * val = module->newConstFloat(node->float_val);

    node->val = val;

    return true;
}

/**
 * @brief 变量声明语句节点翻译成线性中间IR
 * @param node AST节点
 * @return 翻译是否成功，true：成功，false：失败
 */
bool IRGenerator::ir_variable_declare(ast_node * node)
{
    Function * func = module->getCurrentFunction();

    // 函数内定义
    if (func) {
        for (auto child:node->sons) {
            // 检查是否是数组类型且需要计算维度
            if (child->type && child->type->isArrayType() && 
                static_cast<const ArrayType*>(child->type)->getElementType() == nullptr) {
                
                // 这是一个空的数组类型，需要计算维度
                if (child->sons.size() >= 1 && child->sons[0]->node_type == ASTOP(ARRAY_INDICES)) {
                    // 计算数组各维度的值
                    ast_node* arrayIndicesNode = child->sons[0];
                    std::vector<uint32_t> dimensions;
                    
                    for (auto dimExpr : arrayIndicesNode->sons) {
                        if (dimExpr == nullptr) {
                            // 空维度，使用0
                            dimensions.push_back(0);
                        } else {
                            // 计算常量表达式
                            ast_node* CHECK_NODE(dimResult, dimExpr);
                            if (auto constInt = dynamic_cast<ConstInt*>(dimResult->val)) {
                                dimensions.push_back(constInt->getVal());
                            } else if (auto constFloat = dynamic_cast<ConstFloat*>(dimResult->val)) {
                                dimensions.push_back((uint32_t)constFloat->getVal());
                            } else {
                                dimensions.push_back(1); // 默认值
                            }
                        }
                    }
                    
                    // 使用计算出的维度创建新的数组类型
                    child->type = ArrayType::createMultiDimensional(nullptr, dimensions);
                }
            }
            
            Value *val = module->newVarValue(child->type, child->name);
            child->val = val;
            
            if (!child->sons.empty()) {
                // 处理初始值赋值
                int initNodeIndex = child->type && child->type->isArrayType() ? 1 : 0;
                if (child->sons.size() > initNodeIndex) {
                    ast_node * CHECK_NODE(s, child->sons[initNodeIndex]);
                    if (s->node_type == ASTOP(ARRAY_INIT)) {
                        Type *baseType = child->type;
                        for (size_t i=0, l=s->sons.size(); i<l; i++) {
                            ast_node *CHECK_NODE(item, s->sons[i]);
                            node->blockInsts.addInst(item->blockInsts);
                            Instruction *ptr = new BinaryInstruction(
                                func, IRINST_OP_GEP, val, module->newConstInt(i), baseType
                            );
                            node->blockInsts.addInst(ptr);
                            node->blockInsts.addInst(new StoreInstruction(func, ptr, item->val));
                        }
                    } else {
                        node->blockInsts.addInst(s->blockInsts);
                        node->blockInsts.addInst(new MoveInstruction(func, child->val, s->val));
                    }
                }
            }
        }
    } else for (auto child:node->sons) {
        // 全局变量定义，类似处理
        if (child->type && child->type->isArrayType() && 
            static_cast<const ArrayType*>(child->type)->getElementType() == nullptr) {
            
            if (child->sons.size() >= 1 && child->sons[0]->node_type == ASTOP(ARRAY_INDICES)) {
                ast_node* arrayIndicesNode = child->sons[0];
                std::vector<uint32_t> dimensions;
                
                for (auto dimExpr : arrayIndicesNode->sons) {
                    if (dimExpr == nullptr) {
                        dimensions.push_back(0);
                    } else {
                        ast_node* CHECK_NODE(dimResult, dimExpr);
                        if (auto constInt = dynamic_cast<ConstInt*>(dimResult->val)) {
                            dimensions.push_back(constInt->getVal());
                        } else if (auto constFloat = dynamic_cast<ConstFloat*>(dimResult->val)) {
                            dimensions.push_back((uint32_t)constFloat->getVal());
                        } else {
                            dimensions.push_back(1);
                        }
                    }
                }
                
                child->type = ArrayType::createMultiDimensional(nullptr, dimensions);
            }
        }
        
        child->val = module->newVarValue(child->type, child->name);
        if (!child->sons.empty()) {
            int initNodeIndex = child->type && child->type->isArrayType() ? 1 : 0;
            if (child->sons.size() > initNodeIndex) {
                ast_node *CHECK_NODE(s, child->sons[initNodeIndex]);
                if (Instanceof(cexp, ConstInt *, s->val)) {
                    Instanceof(gVal, GlobalVariable*, child->val);
                    gVal->intVal = cexp->getVal();
                }
            }
        }
    }
    return true;
}

/// @brief 分支语句
bool IRGenerator::ir_branch(ast_node * node)
{
    ast_node * CHECK_NODE(cond, node->sons[0]);
    ast_node * CHECK_NODE(if1, node->sons[1]);
    ast_node * if0 = nullptr;
    if (node->sons.size() == 3) { // 有 else
        CHECK_NODE(if0, node->sons[2]);
    }

    node->blockInsts.addInst(cond->blockInsts);
    auto func = module->getCurrentFunction();
    auto lab1 = new LabelInstruction(func);
    auto lab0 = new LabelInstruction(func);
    backPatch(cond->truelist, lab1);
    backPatch(cond->falselist, lab0);
    cond->truelist->clear();
    cond->falselist->clear();

    // if0:
    if (if0) {
        node->blockInsts.addInst(lab0);
        node->blockInsts.addInst(if0->blockInsts);
        lab0 = new LabelInstruction(func);
        // br exit
        node->blockInsts.addInst(new GotoInstruction(func, lab0));
    }

    node->blockInsts.addInst(lab1);
    node->blockInsts.addInst(if1->blockInsts);
    // if0:
    node->blockInsts.addInst(lab0);

    return true;
}

/**
 * @brief 循环语句
 * 将while的条件语句放在循环体后面，在入口前加入一个goto语句进入到条件语句处，回填处
 * 理更方便，且与do-while语句兼容。
 */
bool IRGenerator::ir_loop(ast_node * node)
{
    int isWhile = node->node_type == ast_operator_type::AST_OP_WHILE;
    ast_node * CHECK_NODE(cond, node->sons[!isWhile]);

    auto func = module->getCurrentFunction();
    auto entry = new LabelInstruction(func);
    backPatch(cond->truelist, entry);
    cond->truelist->clear();

    db mlabs;

    LabelInstruction * expr = nullptr;
    if (isWhile) { // 添加goto跳转到条件式
        expr = new LabelInstruction(func);
        node->blockInsts.addInst(new GotoInstruction(func, expr));
        mlabs.a = expr;
    } else
        mlabs.a = entry;

    auto ext = new LabelInstruction(func);
    mlabs.b = ext;
    SLIST_INSERT_HEAD(&labs, &mlabs, entries);
    ast_node * CHECK_NODE(body, node->sons[isWhile]);
    SLIST_REMOVE_HEAD(&labs, entries);

    node->blockInsts.addInst(entry);
    node->blockInsts.addInst(body->blockInsts);
    if (expr)
        node->blockInsts.addInst(expr);
    backPatch(cond->falselist, ext);
    cond->falselist->clear();
    node->blockInsts.addInst(cond->blockInsts);

    node->blockInsts.addInst(ext);
    return true;
}

bool IRGenerator::ir_or(ast_node * node)
{
    ast_node * CHECK_NODE(left, node->sons[0]);
    ast_node * CHECK_NODE(right, node->sons[1]);

    auto flab = new LabelInstruction(module->getCurrentFunction());
    backPatch(left->falselist, flab);
    node->truelist = merge(left->truelist, right->truelist);
    node->falselist = right->falselist;
    node->blockInsts.addInst(left->blockInsts);
    node->blockInsts.addInst(flab);
    node->blockInsts.addInst(right->blockInsts);
    return true;
}

bool IRGenerator::ir_and(ast_node * node)
{
    ast_node * CHECK_NODE(left, node->sons[0]);
    ast_node * CHECK_NODE(right, node->sons[1]);

    auto tlab = new LabelInstruction(module->getCurrentFunction());
    backPatch(left->truelist, tlab);
    node->falselist = merge(left->falselist, right->falselist);
    node->truelist = right->truelist;
    node->blockInsts.addInst(left->blockInsts);
    node->blockInsts.addInst(tlab);
    node->blockInsts.addInst(right->blockInsts);

    return true;
}

bool IRGenerator::ir_not(ast_node * node)
{
    /* Sys2022的NOT操作只针对算术表达式，不针对关系表达式，
     * 构建AST时已将关系表达式内所有的NOT节点转为==0操作，此处只考虑算术表达式内
     */
    ast_node * CHECK_NODE(kid, node->sons[0]);
    node->blockInsts.addInst(kid->blockInsts);

    // TODO float cmp
    auto func = module->getCurrentFunction();
    Value * v = kid->val;
    if (v->getType() != IntegerType::getTypeBool()) {
        v = new BinaryInstruction(func, IROP(INE), kid->val, module->newConstInt(0), IntegerType::getTypeBool());
        node->blockInsts.addInst((Instruction *) v);
    }
    auto i = new BinaryInstruction(func, IROP(XOR), v, module->newConstInt(1), IntegerType::getTypeBool());
    node->blockInsts.addInst(i);
    node->val = i;
    return true;
}

bool IRGenerator::ir_jump(ast_node * node)
{
    if (SLIST_EMPTY(&labs))
        return false;
    db * d = SLIST_FIRST(&labs);
    node->blockInsts.addInst(
        new GotoInstruction(module->getCurrentFunction(), node->node_type == ASTOP(BREAK) ? d->b : d->a));
    return true;
}

IRInstOperator irtype(ast_operator_type type)
{
    switch (type) {
        case ASTOP(ADD):
            return IROP(IADD);
        case ASTOP(SUB):
            return IROP(ISUB);
        case ASTOP(MUL):
            return IROP(IMUL);
        case ASTOP(DIV):
            return IROP(IDIV);
        case ASTOP(MOD):
            return IROP(IMOD);
        case ASTOP(EQ):
            return IROP(IEQ);
        case ASTOP(NE):
            return IROP(INE);
        case ASTOP(GT):
            return IROP(IGT);
        case ASTOP(GE):
            return IROP(IGE);
        case ASTOP(LT):
            return IROP(ILT);
        case ASTOP(LE):
            return IROP(ILE);
        default:
            return IROP(MAX);
    }
}

std::vector<LabelInstruction **> * merge(std::vector<LabelInstruction **> * a, std::vector<LabelInstruction **> * b)
{
    a->insert(a->end(), b->begin(), b->end());
    b->clear();
    return a;
}

/// @brief 数组访问节点翻译成线性中间IR
/// @param node AST节点
/// @return 翻译是否成功，true：成功，false：失败
bool IRGenerator::ir_array_access(ast_node * node)
{
    ast_node * arrayNameNode = node->sons[0];
    ast_node * indicesNode = node->sons[1];

    // 解析数组名
    if (!ir_node_var_id(arrayNameNode)) {
        return false;
    }

    // 获取数组变量
    Value * arrayVal = arrayNameNode->val;
    if (!arrayVal || !arrayVal->getType()->isArrayType()) {
        return false;
    }

    const Type * tp = arrayVal->getType();
    
    Function * func = module->getCurrentFunction();

    for (auto indexNode : indicesNode->sons) {
        if (!tp->isArrayType()) {
            // 维度过多
            return false;
        }

        ast_node *CHECK_NODE(indexExpr, indexNode);
        // TODO 数组越界检查
        node->blockInsts.addInst(indexExpr->blockInsts);
        Instruction *getptr = new BinaryInstruction(
            func, IRINST_OP_GEP, arrayVal, indexExpr->val, (Type*)tp
        );
        arrayVal = getptr;
        node->blockInsts.addInst(getptr);

        tp = ((ArrayType*)tp)->getElementType();
    }
    node->val = arrayVal;
    node->type = (Type*)tp;

    return true;
}

bool IRGenerator::ir_lval_to_r(ast_node * node)
{
    ast_node *CHECK_NODE(s, node->sons[0]);

    node->blockInsts.addInst(s->blockInsts);
    Type *tp = s->type;
    Value *v = s->val;
    if (s->node_type == ASTOP(ARRAY_ACCESS)) {
        auto func = module->getCurrentFunction();
        auto ldr = new LoadInstruction(func, v, tp);
        node->blockInsts.addInst(ldr);
        v = ldr;
    }
    node->type = tp;
    node->val = v;
    return true;
}

/// @brief 数组初始化节点翻译成线性中间IR
/// @param node AST节点
/// @return 翻译是否成功，true：成功，false：失败
bool IRGenerator::ir_array_init(ast_node * node)
{
    // TODO: 实现数组初始化的IR生成
    // 这里需要根据初始化列表生成相应的赋值指令
    
    //Function * currentFunc = module->getCurrentFunction();
    for (size_t i=0; i<node->sons.size(); i++) {
        ast_node *el = node->sons[i];
        if (el->node_type == ASTOP(ARRAY_INIT)) {
            ir_array_init(el);
            auto x = node->sons.begin()+i;
            node->sons.erase(x);
            node->sons.insert(x, el->sons.begin(), el->sons.end());
        }
    }
    
    return true;
}
