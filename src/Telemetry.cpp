#include "Telemetry.h"

#include <algorithm>
#include <fstream>
#include <iostream>

#include "IRMutator.h"
#include "Util.h"

namespace Halide {
namespace Internal {

namespace {

// TODO: for now we are just going to ignore potential issues with
// static-initialization-order-fiasco, as Telemetry isn't currently used
// from any static-initialization execution scope.
std::unique_ptr<Telemetry> active_telemetry;

class AnonymizeNames : public IRMutator {
    using IRMutator::visit;

    std::map<std::string, std::string> remapping;

    // TODO: is this the right way to anonymize extern calls and image input names?
    Expr visit(const Call *op) override {
        if (op->call_type == Call::Extern || op->call_type == Call::ExternCPlusPlus) {
            return Variable::make(op->type, remap(op->name, "define_extern_"));
        } else if (op->call_type == Call::Image) {
            return Variable::make(op->type, remap(op->name, "image"));
        } else {
            return op;
        }

    }

    Expr visit(const Variable *op) override {
        return Variable::make(op->type, remap_var_name(op->name));
    }

    std::string remap(const std::string &var_name, const char *replacement) {
        std::string anon_name = replacement + std::to_string(remapping.size());
        auto p = remapping.insert({var_name, anon_name});
        // debug(0) << "remap " << var_name << " -> " << anon_name << " -> " << p.first->second << "\n";
        return p.first->second;
    }

public:
    std::string remap_var_name(const std::string &var_name) {
        return remap(var_name, "anon");
    }
};

}  // namespace

void set_telemetry(std::unique_ptr<Telemetry> telemetry) {
    if (telemetry) {
        active_telemetry = std::move(telemetry);
    } else {
        active_telemetry.reset();
    }
}

Telemetry *get_telemetry() {
    return active_telemetry.get();
}

BasicTelemetry::BasicTelemetry(const std::string &output_path)
    : output_path(output_path) {
}

void BasicTelemetry::record_matched_simplifier_rule(const std::string &rulename) {
    matched_simplifier_rules[rulename] += 1;
}

void BasicTelemetry::record_non_monotonic_loop_var(const std::string &loop_var, Expr expr) {
    non_monotonic_loop_vars.emplace_back(loop_var, std::move(expr));
}
void BasicTelemetry::record_failed_to_prove(Expr failed_to_prove, Expr original_expr) {
    failed_to_prove_exprs.emplace_back(std::move(failed_to_prove), std::move(original_expr));
}

void BasicTelemetry::anonymize() {
    // Normally we want to anonymize everything, but we can defeat this
    // for debugging purposes.
    std::string no_anon = get_env_variable("HL_TELEMETRY_NO_ANONYMIZE");
    if (no_anon == "1") {
        return;
    }

    AnonymizeNames anonymizer;
    for (auto &it : non_monotonic_loop_vars) {
        it.first = anonymizer.remap_var_name(it.first);
        it.second = anonymizer.mutate(it.second);
    }
    for (auto &it : failed_to_prove_exprs) {
        it.first = anonymizer.mutate(it.first);
        it.second = anonymizer.mutate(it.second);
    }
}

void BasicTelemetry::finalize() {
    anonymize();

    std::ofstream fout;
    if (!output_path.empty()) {
        fout.open(output_path);
    }
    std::ostream &f = output_path.empty() ? std::cerr : fout;

    // Output in JSON form (well, almost; we don't leave out trailing commas).
    // Use std::set as a handy way to avoid dupes and optionally sort.

    f << "{\n"
      << " \"name\": \"BasicTelemetry\",\n";

    {
        using P = std::pair<std::string, int64_t>;
        // Sort these in descending order by usage
        struct Compare {
            bool operator()(const P &a, const P &b) const {
                return a.second > b.second;
            }
        };

        std::set<P, Compare> sorted;
        for (const auto &it : matched_simplifier_rules) {
            sorted.emplace(it.first, it.second);
        }

        f << " \"matched_simplifier_rules\": {\n";
        for (const auto &it : sorted) {
            f << "  \"" << it.first << "\" : " << it.second << ",\n";
        }
        f << " },\n";
    }

    {
        using P = std::pair<std::string, std::string>;

        std::set<P> sorted;
        for (const auto &it : non_monotonic_loop_vars) {
            std::ostringstream s;
            s << it.second;
            sorted.emplace(it.first, s.str());
        }

        f << " \"non_monotonic_loop_vars\": {\n";
        for (const auto &it : sorted) {
            f << "  \"" << it.first << "\" : " << it.second << ",\n";
        }
        f << " },\n";
    }

    {
        using P = std::pair<std::string, std::string>;

        std::set<P> sorted;
        for (const auto &it : failed_to_prove_exprs) {
            std::ostringstream s1, s2;
            s1 << it.first;
            s2 << it.second;
            sorted.emplace(s1.str(), s2.str());
        }

        f << " \"failed_to_prove\": {\n";
        for (const auto &it : sorted) {
            f << "  \"" << it.first << "\" : " << it.second << ",\n";
        }
        f << " },\n";
    }

    f << "}\n";

    f.flush();
}

std::unique_ptr<Telemetry> BasicTelemetry::from_env() {
    std::unique_ptr<Telemetry> result;

    std::string path = get_env_variable("HL_TELEMETRY");
    if (path.empty() || path == "0") {
        // leave result null
    } else if (path == "1") {
        result.reset(new BasicTelemetry());
    } else {
        result.reset(new BasicTelemetry(path));
    }
    return result;
}

}  // namespace Internal
}  // namespace Halide
