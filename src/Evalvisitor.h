#pragma once
#ifndef PYTHON_INTERPRETER_EVALVISITOR_H
#define PYTHON_INTERPRETER_EVALVISITOR_H

#include "Python3ParserBaseVisitor.h"
#include <any>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <functional>

class Value {
public:
    enum Type {
        NONE,
        BOOL,
        INT,
        FLOAT,
        STRING,
        TUPLE
    };

private:
    Type type;
    std::any data;

public:
    Value() : type(NONE) {}
    Value(bool b) : type(BOOL), data(b) {}
    Value(long long i) : type(INT), data(i) {}
    Value(const std::string& s) : type(STRING), data(s) {}
    Value(double f) : type(FLOAT), data(f) {}

    Type getType() const { return type; }

    bool getBool() const {
        if (type == BOOL) return std::any_cast<bool>(data);
        throw std::runtime_error("Not a boolean");
    }

    long long getInt() const {
        if (type == INT) return std::any_cast<long long>(data);
        throw std::runtime_error("Not an integer");
    }

    double getFloat() const {
        if (type == FLOAT) return std::any_cast<double>(data);
        if (type == INT) return static_cast<double>(std::any_cast<long long>(data));
        throw std::runtime_error("Not a float");
    }

    std::string getString() const {
        if (type == STRING) return std::any_cast<std::string>(data);
        throw std::runtime_error("Not a string");
    }

    std::string toString() const {
        switch (type) {
            case NONE: return "None";
            case BOOL: return std::any_cast<bool>(data) ? "True" : "False";
            case INT: return std::to_string(std::any_cast<long long>(data));
            case FLOAT: {
                std::stringstream ss;
                ss << std::fixed << std::setprecision(6) << std::any_cast<double>(data);
                std::string s = ss.str();
                s.erase(s.find_last_not_of('0') + 1, std::string::npos);
                if (s.back() == '.') s.push_back('0');
                return s;
            }
            case STRING: return std::any_cast<std::string>(data);
            case TUPLE: return "()";
            default: return "Unknown";
        }
    }

    Value operator+(const Value& other) const {
        if (type == STRING && other.type == STRING) {
            return Value(getString() + other.getString());
        }
        if (type == INT && other.type == INT) {
            return Value(getInt() + other.getInt());
        }
        double a = getFloat();
        double b = other.getFloat();
        return Value(a + b);
    }

    Value operator-(const Value& other) const {
        if (type == INT && other.type == INT) {
            return Value(getInt() - other.getInt());
        }
        double a = getFloat();
        double b = other.getFloat();
        return Value(a - b);
    }

    Value operator*(const Value& other) const {
        if (type == STRING && other.type == INT) {
            std::string result;
            long long count = other.getInt();
            if (count < 0) count = 0;
            for (long long i = 0; i < count; i++) {
                result += getString();
            }
            return Value(result);
        }
        if (type == INT && other.type == INT) {
            return Value(getInt() * other.getInt());
        }
        double a = getFloat();
        double b = other.getFloat();
        return Value(a * b);
    }

    Value operator/(const Value& other) const {
        double b = other.getFloat();
        if (std::abs(b) < 1e-12) {
            throw std::runtime_error("Division by zero");
        }
        double a = getFloat();
        return Value(a / b);
    }

    bool operator==(const Value& other) const {
        if (type != other.type) {
            if ((type == INT || type == FLOAT) && (other.type == INT || other.type == FLOAT)) {
                return getFloat() == other.getFloat();
            }
            return false;
        }

        switch (type) {
            case NONE: return true;
            case BOOL: return getBool() == other.getBool();
            case INT: return getInt() == other.getInt();
            case FLOAT: return std::abs(getFloat() - other.getFloat()) < 1e-12;
            case STRING: return getString() == other.getString();
            default: return false;
        }
    }

    bool operator!=(const Value& other) const {
        return !(*this == other);
    }

    bool isTruthy() const {
        switch (type) {
            case NONE: return false;
            case BOOL: return std::any_cast<bool>(data);
            case INT: return std::any_cast<long long>(data) != 0;
            case FLOAT: return std::abs(std::any_cast<double>(data)) > 1e-12;
            case STRING: return !std::any_cast<std::string>(data).empty();
            case TUPLE: return false;
            default: return false;
        }
    }
};

class Scope {
private:
    std::unordered_map<std::string, Value> variables;
    Scope* parent;

public:
    Scope(Scope* p = nullptr) : parent(p) {}

    void setVariable(const std::string& name, const Value& value) {
        variables[name] = value;
    }

    Value getVariable(const std::string& name) const {
        auto it = variables.find(name);
        if (it != variables.end()) {
            return it->second;
        }
        if (parent) {
            return parent->getVariable(name);
        }
        throw std::runtime_error("Undefined variable: " + name);
    }

    bool hasVariable(const std::string& name) const {
        if (variables.find(name) != variables.end()) return true;
        if (parent) return parent->hasVariable(name);
        return false;
    }
};

class EvalVisitor : public Python3ParserBaseVisitor {
private:
    std::vector<Scope> scopes;
    std::unordered_map<std::string, std::pair<std::vector<std::string>, antlr4::tree::ParseTree*>> functions;

    bool shouldBreak = false;
    bool shouldContinue = false;
    bool shouldReturn = false;
    Value returnValue;

    Scope& currentScope() {
        return scopes.back();
    }

    void pushScope() {
        scopes.push_back(Scope(scopes.empty() ? nullptr : &scopes.back()));
    }

    void popScope() {
        if (!scopes.empty()) {
            scopes.pop_back();
        }
    }

    // Built-in functions
    Value callPrint(const std::vector<Value>& args);
    Value callInt(const std::vector<Value>& args);
    Value callFloat(const std::vector<Value>& args);
    Value callStr(const std::vector<Value>& args);
    Value callBool(const std::vector<Value>& args);

public:
    EvalVisitor() {
        pushScope();
    }

    // Override visitor methods
    std::any visitFile_input(Python3Parser::File_inputContext *ctx) override;
    std::any visitFuncdef(Python3Parser::FuncdefContext *ctx) override;
    std::any visitStmt(Python3Parser::StmtContext *ctx) override;
    std::any visitSimple_stmt(Python3Parser::Simple_stmtContext *ctx) override;
    std::any visitExpr_stmt(Python3Parser::Expr_stmtContext *ctx) override;
    std::any visitIf_stmt(Python3Parser::If_stmtContext *ctx) override;
    std::any visitWhile_stmt(Python3Parser::While_stmtContext *ctx) override;
    std::any visitTest(Python3Parser::TestContext *ctx) override;
    std::any visitOr_test(Python3Parser::Or_testContext *ctx) override;
    std::any visitAnd_test(Python3Parser::And_testContext *ctx) override;
    std::any visitNot_test(Python3Parser::Not_testContext *ctx) override;
    std::any visitComparison(Python3Parser::ComparisonContext *ctx) override;
    std::any visitArith_expr(Python3Parser::Arith_exprContext *ctx) override;
    std::any visitTerm(Python3Parser::TermContext *ctx) override;
    std::any visitFactor(Python3Parser::FactorContext *ctx) override;
    std::any visitAtom_expr(Python3Parser::Atom_exprContext *ctx) override;
    std::any visitAtom(Python3Parser::AtomContext *ctx) override;
    std::any visitTestlist(Python3Parser::TestlistContext *ctx) override;
    std::any visitTrailer(Python3Parser::TrailerContext *ctx) override;
    std::any visitArglist(Python3Parser::ArglistContext *ctx) override;
    std::any visitArgument(Python3Parser::ArgumentContext *ctx) override;
    std::any visitFlow_stmt(Python3Parser::Flow_stmtContext *ctx) override;
    std::any visitBreak_stmt(Python3Parser::Break_stmtContext *ctx) override;
    std::any visitContinue_stmt(Python3Parser::Continue_stmtContext *ctx) override;
    std::any visitReturn_stmt(Python3Parser::Return_stmtContext *ctx) override;

    // Helper methods
    Value evaluateTest(Python3Parser::TestContext *ctx);
    Value evaluateAtom(Python3Parser::AtomContext *ctx);
    std::vector<Value> evaluateTestlist(Python3Parser::TestlistContext *ctx);
};


#endif//PYTHON_INTERPRETER_EVALVISITOR_H