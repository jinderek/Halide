#ifndef HALIDE_TELEMETRY_H_
#define HALIDE_TELEMETRY_H_

/** \file
 * Defines an interface used to gather compile-time information, stats, etc
 * for use in evaluating internal Halide compilation rules and efficiency.
 *
 * There is a 'standard' implementation that simply logs information to
 * stderr (or a custom file), but the entire implementation can be replaced
 * to log to a custom destination.
 */

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Expr.h"

namespace Halide {
namespace Internal {

class Telemetry {
public:
    Telemetry() = default;
    virtual ~Telemetry() = default;

    /** Record when a particular simplifier rule matches.
     */
    virtual void record_matched_simplifier_rule(const std::string &rulename) = 0;

    /** Record when an expression is non-monotonic in a loop variable.
     */
    virtual void record_non_monotonic_loop_var(const std::string &loop_var, Expr expr) = 0;

    /** Record when can_prove() fails, but cannot find a counterexample.
     */
    virtual void record_failed_to_prove(Expr failed_to_prove, Expr original_expr) = 0;

    /**
     * Finish all data gathering and flush any output buffers / rpcs / etc.
     * The Telemetry object may ignore and/or assert-fail if more logging
     * requests are made after this call.
     */
    virtual void finalize() = 0;
};

/** Set the active Telemetry object, replacing any existing one.
 * It is legal to pass in a nullptr (which means "don't do any telemetry").
 * Generally, this should be called once per compilation session (before
 * any Halide compilation starts); replacing it multiple times is possible
 * but requires care to get useful stats and is not recommended. */
void set_telemetry(std::unique_ptr<Telemetry> telemetry);

/** Return the currently active Telemetry object. If set_telemetry()
 * has never been called, a nullptr implementation will be returned.
 * Do not save the pointer returned! It is intended to be used for immediate
 * calls only. */
Telemetry *get_telemetry();

/** BasicTelemetry is a basic implementation of the Telemetry interface
 * that saves logged data, then logs it all to a file in finalize().
 *
 * It's designed to allow subclasses to override just finalize() in order
 * to allow logging to non-file destinations.
 */
class BasicTelemetry : public Telemetry {
public:
    /** Create a BasicTelemetry that logs to stderr. */
    BasicTelemetry() = default;

    /** Create a BasicTelemetry that logs to the given file. */
    BasicTelemetry(const std::string &output_path);

    void record_matched_simplifier_rule(const std::string &rulename) override;
    void record_non_monotonic_loop_var(const std::string &loop_var, Expr expr) override;
    void record_failed_to_prove(Expr failed_to_prove, Expr original_expr) override;
    void finalize() override;

    /**
     * Create a BasicTelemetry based on the HL_TELEMETRY env var:
     * - if it is undefined (or empty, or "0"), return nullptr
     * - if it is set to "1", return a BasicTelemetry that outputs to stderr
     * - if it is set to any other string, assume that string is a file path
     *   and return a BasicTelemetry that outputs to that file
     */
    static std::unique_ptr<Telemetry> from_env();

protected:
    std::string output_path;
    std::map<std::string, int64_t> matched_simplifier_rules;
    // TODO: are we likely to see enough duplicates here to make
    // it worth considering a set instead of a vector?
    std::vector<std::pair<std::string, Expr>> non_monotonic_loop_vars;
    std::vector<std::pair<Expr, Expr>> failed_to_prove_exprs;

    void anonymize();
};

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_TELEMETRY_H_
