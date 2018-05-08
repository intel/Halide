#include "UnifyDuplicateLets.h"
#include "IRMutator.h"
#include "IREquality.h"
#include <map>

namespace Halide {
namespace Internal {

using std::map;
using std::string;

class UnifyDuplicateLets : public IRMutator2 {
    using IRMutator2::visit;

    map<Expr, string, IRDeepCompare> scope;
    map<string, string> rewrites;
    string producing;

public:
    using IRMutator2::mutate;

    Expr mutate(const Expr &e) override {
        if (e.defined()) {
            map<Expr, string, IRDeepCompare>::iterator iter = scope.find(e);
            if (iter != scope.end()) {
                return Variable::make(e.type(), iter->second);
            } else {
                return IRMutator2::mutate(e);
            }
        } else {
            return Expr();
        }
    }

protected:
    Expr visit(const Variable *op) override {
        map<string, string>::iterator iter = rewrites.find(op->name);
        if (iter != rewrites.end()) {
            return Variable::make(op->type, iter->second);
        } else {
            return op;
        }
    }

    // Can't unify lets where the RHS might be not be pure
    bool is_impure;
    Expr visit(const Call *op) override {
        is_impure |= !op->is_pure();
        return IRMutator2::visit(op);
    }

    Expr visit(const Load *op) override {
        is_impure |= ((op->name == producing) ||
                      starts_with(op->name + ".", producing));
        return IRMutator2::visit(op);
    }

    Stmt visit(const ProducerConsumer *op) override {
        if (op->is_producer) {
            string old_producing = producing;
            producing = op->name;
            Stmt stmt = IRMutator2::visit(op);
            producing = old_producing;
            return stmt;
        } else {
            return IRMutator2::visit(op);
        }
    }

    Stmt visit(const LetStmt *op) override {
        is_impure = false;
        Expr value = mutate(op->value);
        Stmt body = op->body;

        bool should_pop = false;
        bool should_erase = false;

        if (!is_impure) {
            map<Expr, string, IRDeepCompare>::iterator iter = scope.find(value);
            if (iter == scope.end()) {
                scope[value] = op->name;
                should_pop = true;
            } else {
                value = Variable::make(value.type(), iter->second);
                rewrites[op->name] = iter->second;
                should_erase = true;
            }
        }

        body = mutate(op->body);

        if (should_pop) {
            scope.erase(value);
        }
        if (should_erase) {
            rewrites.erase(op->name);
        }

        if (value.same_as(op->value) && body.same_as(op->body)) {
            return op;
        } else {
            return LetStmt::make(op->name, value, body);
        }
    }
};

Stmt unify_duplicate_lets(Stmt s) {
    return UnifyDuplicateLets().mutate(s);
}

}
}
