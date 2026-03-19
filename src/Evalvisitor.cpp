#include "Evalvisitor.h"
#include "Python3Parser.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>

using namespace antlr4;

// EvalVisitor implementation
std::any EvalVisitor::visitFile_input(Python3Parser::File_inputContext *ctx) {
    // Visit all statements
    for (auto stmt : ctx->stmt()) {
        try {
            visit(stmt);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }

        if (shouldReturn) {
            break;
        }
    }
    return {};
}

std::any EvalVisitor::visitFuncdef(Python3Parser::FuncdefContext *ctx) {
    std::string funcName = ctx->NAME()->getText();

    // Get parameters
    std::vector<std::string> params;
    auto typedargslist = ctx->parameters()->typedargslist();
    if (typedargslist) {
        auto paramList = typedargslist->tfpdef();
        for (auto param : paramList) {
            params.push_back(param->NAME()->getText());
        }
    }

    // Store function definition
    functions[funcName] = {params, ctx->suite()};

    return {};
}

std::any EvalVisitor::visitStmt(Python3Parser::StmtContext *ctx) {
    if (ctx->simple_stmt()) {
        return visit(ctx->simple_stmt());
    } else if (ctx->compound_stmt()) {
        return visit(ctx->compound_stmt());
    }
    return {};
}

std::any EvalVisitor::visitSimple_stmt(Python3Parser::Simple_stmtContext *ctx) {
    return visit(ctx->small_stmt());
}

std::any EvalVisitor::visitExpr_stmt(Python3Parser::Expr_stmtContext *ctx) {
    auto testlists = ctx->testlist();

    if (testlists.empty()) {
        return {};
    }

    if (ctx->augassign()) {
        // Augmented assignment - skip for now
        return {};
    } else if (testlists.size() > 1) {
        // Assignment: x = 1 or x = y = 1
        auto rightValues = evaluateTestlist(testlists.back());

        if (!rightValues.empty()) {
            // Handle single assignment: x = value
            auto leftTests = testlists[0]->test();
            if (leftTests.size() == 1) {
                // Get the left expression
                auto leftExpr = leftTests[0];
                // Check if it's a simple variable name
                auto atomExpr = leftExpr->or_test()->and_test(0)->not_test(0)->comparison()->arith_expr(0)->term(0)->factor(0)->atom_expr();
                if (atomExpr && atomExpr->atom() && atomExpr->atom()->NAME()) {
                    std::string varName = atomExpr->atom()->NAME()->getText();
                    currentScope().setVariable(varName, rightValues[0]);
                }
            }
        }
    } else {
        // Expression statement: just evaluate it
        evaluateTestlist(testlists[0]);
    }

    return {};
}

std::any EvalVisitor::visitIf_stmt(Python3Parser::If_stmtContext *ctx) {
    auto condition = evaluateTest(ctx->test(0));

    if (condition.isTruthy()) {
        visit(ctx->suite(0));
    } else {
        // Check elif
        auto elifTests = ctx->test();
        for (size_t i = 1; i < elifTests.size(); i++) {
            if (evaluateTest(elifTests[i]).isTruthy()) {
                visit(ctx->suite(i));
                return {};
            }
        }

        // Else
        if (ctx->ELSE()) {
            visit(ctx->suite().back());
        }
    }

    return {};
}

std::any EvalVisitor::visitWhile_stmt(Python3Parser::While_stmtContext *ctx) {
    while (true) {
        auto condition = evaluateTest(ctx->test());
        if (!condition.isTruthy()) break;

        visit(ctx->suite());

        if (shouldBreak) {
            shouldBreak = false;
            break;
        }

        if (shouldContinue) {
            shouldContinue = false;
            continue;
        }

        if (shouldReturn) break;
    }

    return {};
}

std::any EvalVisitor::visitTest(Python3Parser::TestContext *ctx) {
    return visit(ctx->or_test());
}

std::any EvalVisitor::visitOr_test(Python3Parser::Or_testContext *ctx) {
    // For now, just visit the first and_test
    if (!ctx->and_test().empty()) {
        return visit(ctx->and_test(0));
    }

    return std::any(Value());
}

std::any EvalVisitor::visitAnd_test(Python3Parser::And_testContext *ctx) {
    // For now, just visit the first not_test
    if (!ctx->not_test().empty()) {
        return visit(ctx->not_test(0));
    }

    return std::any(Value());
}

std::any EvalVisitor::visitNot_test(Python3Parser::Not_testContext *ctx) {
    if (ctx->NOT()) {
        // Visit the nested not_test
        if (ctx->not_test()) {
            auto result = visit(ctx->not_test());
            try {
                Value value = std::any_cast<Value>(result);
                return std::any(Value(!value.isTruthy()));
            } catch (...) {
                return std::any(Value(false));
            }
        }
        return std::any(Value(false));
    } else {
        return visit(ctx->comparison());
    }
}

std::any EvalVisitor::visitComparison(Python3Parser::ComparisonContext *ctx) {
    auto arithExprs = ctx->arith_expr();
    auto compOps = ctx->comp_op();

    if (arithExprs.empty()) {
        return std::any(Value());
    }

    if (arithExprs.size() == 1) {
        return visit(arithExprs[0]);
    }

    // Evaluate first expression
    auto firstResult = visit(arithExprs[0]);
    Value left;
    try {
        left = std::any_cast<Value>(firstResult);
    } catch (...) {
        left = Value();
    }

    // Simple comparison of first two expressions
    auto secondResult = visit(arithExprs[1]);
    Value right;
    try {
        right = std::any_cast<Value>(secondResult);
    } catch (...) {
        right = Value();
    }

    bool result = false;
    if (compOps.size() > 0) {
        std::string op = compOps[0]->getText();
        if (op == "<") {
            result = left < right;
        } else if (op == ">") {
            result = left > right;
        } else if (op == "==") {
            result = left == right;
        } else if (op == "!=") {
            result = left != right;
        } else if (op == "<=") {
            result = left <= right;
        } else if (op == ">=") {
            result = left >= right;
        }
    }

    return std::any(Value(result));
}

std::any EvalVisitor::visitArith_expr(Python3Parser::Arith_exprContext *ctx) {
    auto terms = ctx->term();
    auto ops = ctx->addorsub_op();

    if (terms.empty()) {
        return std::any(Value());
    }

    // Evaluate first term
    auto firstResult = visit(terms[0]);
    Value result;
    try {
        result = std::any_cast<Value>(firstResult);
    } catch (...) {
        result = Value();
    }

    // Apply operations
    for (size_t i = 1; i < terms.size(); i++) {
        auto termResult = visit(terms[i]);
        Value termValue;
        try {
            termValue = std::any_cast<Value>(termResult);
        } catch (...) {
            termValue = Value();
        }

        if (i-1 < ops.size()) {
            std::string op = ops[i-1]->getText();
            if (op == "+") {
                result = result + termValue;
            } else if (op == "-") {
                result = result - termValue;
            }
        }
    }

    return std::any(result);
}

std::any EvalVisitor::visitTerm(Python3Parser::TermContext *ctx) {
    auto factors = ctx->factor();
    auto ops = ctx->muldivmod_op();

    if (factors.empty()) {
        return std::any(Value());
    }

    // Evaluate first factor
    auto firstResult = visit(factors[0]);
    Value result;
    try {
        result = std::any_cast<Value>(firstResult);
    } catch (...) {
        result = Value();
    }

    // Apply operations
    for (size_t i = 1; i < factors.size(); i++) {
        auto factorResult = visit(factors[i]);
        Value factorValue;
        try {
            factorValue = std::any_cast<Value>(factorResult);
        } catch (...) {
            factorValue = Value();
        }

        if (i-1 < ops.size()) {
            std::string op = ops[i-1]->getText();
            if (op == "*") {
                result = result * factorValue;
            } else if (op == "/") {
                result = result / factorValue;
            }
            // TODO: Handle // and %
        }
    }

    return std::any(result);
}

std::any EvalVisitor::visitFactor(Python3Parser::FactorContext *ctx) {
    if (ctx->factor()) {
        // Unary minus
        auto result = visit(ctx->factor());
        try {
            Value value = std::any_cast<Value>(result);
            if (value.getType() == Value::INT) {
                return std::any(Value(-value.getInt()));
            } else if (value.getType() == Value::FLOAT) {
                return std::any(Value(-value.getFloat()));
            }
        } catch (...) {
            return std::any(Value());
        }
    }

    return visit(ctx->atom_expr());
}

std::any EvalVisitor::visitAtom_expr(Python3Parser::Atom_exprContext *ctx) {
    if (ctx->trailer()) {
        // Function call
        auto trailer = ctx->trailer();

        // Get function name from atom
        std::string funcName;
        if (ctx->atom()->NAME()) {
            funcName = ctx->atom()->NAME()->getText();
        }

        // Get arguments
        std::vector<Value> args;
        if (trailer->arglist()) {
            auto arguments = trailer->arglist()->argument();
            for (auto arg : arguments) {
                if (arg->test().size() > 0) {
                    args.push_back(evaluateTest(arg->test(0)));
                }
            }
        }

        // Call built-in functions
        if (funcName == "print") {
            return std::any(callPrint(args));
        } else if (funcName == "int") {
            return std::any(callInt(args));
        } else if (funcName == "float") {
            return std::any(callFloat(args));
        } else if (funcName == "str") {
            return std::any(callStr(args));
        } else if (funcName == "bool") {
            return std::any(callBool(args));
        }

        // TODO: Handle user-defined functions
    } else {
        auto atomResult = visit(ctx->atom());
        Value atomValue;

        try {
            atomValue = std::any_cast<Value>(atomResult);
        } catch (...) {
            atomValue = Value();
        }

        return std::any(atomValue);
    }

    return std::any(Value());
}

std::any EvalVisitor::visitAtom(Python3Parser::AtomContext *ctx) {
    if (ctx->NAME()) {
        // Variable reference
        std::string varName = ctx->NAME()->getText();
        try {
            return std::any(currentScope().getVariable(varName));
        } catch (...) {
            return std::any(Value()); // Return None if undefined
        }
    } else if (ctx->NUMBER()) {
        // Number
        std::string numStr = ctx->NUMBER()->getText();
        if (numStr.find('.') != std::string::npos) {
            return std::any(Value(std::stod(numStr)));
        } else {
            return std::any(Value(std::stoll(numStr)));
        }
    } else if (!ctx->STRING().empty()) {
        // String
        std::string str = ctx->STRING(0)->getText();
        str = str.substr(1, str.length() - 2);
        return std::any(Value(str));
    } else if (ctx->TRUE()) {
        return std::any(Value(true));
    } else if (ctx->FALSE()) {
        return std::any(Value(false));
    } else if (ctx->NONE()) {
        return std::any(Value());
    } else if (ctx->test()) {
        return visit(ctx->test());
    }

    return std::any(Value());
}

std::any EvalVisitor::visitTestlist(Python3Parser::TestlistContext *ctx) {
    auto tests = ctx->test();

    if (tests.empty()) {
        return std::any(std::vector<Value>());
    }

    if (tests.size() == 1) {
        auto value = evaluateTest(tests[0]);
        std::vector<Value> result = {value};
        return std::any(result);
    }

    // Multiple values
    std::vector<Value> result;
    for (auto test : tests) {
        result.push_back(evaluateTest(test));
    }
    return std::any(result);
}

std::any EvalVisitor::visitTrailer(Python3Parser::TrailerContext *ctx) {
    return {};
}

std::any EvalVisitor::visitArglist(Python3Parser::ArglistContext *ctx) {
    return {};
}

std::any EvalVisitor::visitArgument(Python3Parser::ArgumentContext *ctx) {
    return {};
}

std::any EvalVisitor::visitFlow_stmt(Python3Parser::Flow_stmtContext *ctx) {
    return visitChildren(ctx);
}

std::any EvalVisitor::visitBreak_stmt(Python3Parser::Break_stmtContext *ctx) {
    shouldBreak = true;
    return {};
}

std::any EvalVisitor::visitContinue_stmt(Python3Parser::Continue_stmtContext *ctx) {
    shouldContinue = true;
    return {};
}

std::any EvalVisitor::visitReturn_stmt(Python3Parser::Return_stmtContext *ctx) {
    shouldReturn = true;
    if (ctx->testlist()) {
        auto values = evaluateTestlist(ctx->testlist());
        if (!values.empty()) {
            returnValue = values[0];
        }
    }
    return {};
}

// Helper method implementations
Value EvalVisitor::evaluateTest(Python3Parser::TestContext *ctx) {
    auto result = visit(ctx);
    try {
        return std::any_cast<Value>(result);
    } catch (...) {
        return Value();
    }
}

Value EvalVisitor::evaluateAtom(Python3Parser::AtomContext *ctx) {
    auto result = visit(ctx);
    try {
        return std::any_cast<Value>(result);
    } catch (...) {
        return Value();
    }
}

std::vector<Value> EvalVisitor::evaluateTestlist(Python3Parser::TestlistContext *ctx) {
    auto result = visit(ctx);
    try {
        return std::any_cast<std::vector<Value>>(result);
    } catch (...) {
        return {};
    }
}

// Built-in functions implementation
Value EvalVisitor::callPrint(const std::vector<Value>& args) {
    for (size_t i = 0; i < args.size(); i++) {
        std::cout << args[i].toString();
        if (i != args.size() - 1) {
            std::cout << " ";
        }
    }
    std::cout << std::endl;
    return Value(); // print returns None
}

Value EvalVisitor::callInt(const std::vector<Value>& args) {
    if (args.size() != 1) {
        throw std::runtime_error("int() takes exactly one argument");
    }

    const Value& arg = args[0];
    switch (arg.getType()) {
        case Value::BOOL:
            return Value(arg.getBool() ? 1LL : 0LL);
        case Value::INT:
            return arg;
        case Value::FLOAT:
            return Value(static_cast<long long>(arg.getFloat()));
        case Value::STRING: {
            std::string s = arg.getString();
            try {
                if (s.find('.') != std::string::npos) {
                    double d = std::stod(s);
                    return Value(static_cast<long long>(d));
                } else {
                    return Value(std::stoll(s));
                }
            } catch (...) {
                throw std::runtime_error("Invalid literal for int(): " + s);
            }
        }
        default:
            throw std::runtime_error("int() argument must be a number or string");
    }
}

Value EvalVisitor::callFloat(const std::vector<Value>& args) {
    if (args.size() != 1) {
        throw std::runtime_error("float() takes exactly one argument");
    }

    const Value& arg = args[0];
    switch (arg.getType()) {
        case Value::BOOL:
            return Value(arg.getBool() ? 1.0 : 0.0);
        case Value::INT:
            return Value(static_cast<double>(arg.getInt()));
        case Value::FLOAT:
            return arg;
        case Value::STRING: {
            std::string s = arg.getString();
            try {
                return Value(std::stod(s));
            } catch (...) {
                throw std::runtime_error("Invalid literal for float(): " + s);
            }
        }
        default:
            throw std::runtime_error("float() argument must be a number or string");
    }
}

Value EvalVisitor::callStr(const std::vector<Value>& args) {
    if (args.size() != 1) {
        throw std::runtime_error("str() takes exactly one argument");
    }

    return Value(args[0].toString());
}

Value EvalVisitor::callBool(const std::vector<Value>& args) {
    if (args.size() != 1) {
        throw std::runtime_error("bool() takes exactly one argument");
    }

    return Value(args[0].isTruthy());
}