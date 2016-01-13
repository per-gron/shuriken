#pragma once

#include <unordered_set>
#include <vector>

#include "build_status.h"
#include "command_runner.h"
#include "file_system.h"
#include "invocation_log.h"
#include "invocations.h"
#include "step.h"
#include "task.h"

namespace shk {

/**
 * Find build invocations that are in the invocation log but no longer exist in
 * the Manifest graph. Their outputs should be deleted.
 *
 * The function returns a list of build step hashes that can be found as keys in
 * invocations.entries.
 */
std::vector<Hash> staleInvocations(
    const std::unordered_set<Hash> &step_hashes,
    const Invocations &invocations);

/**
 * Find invocations that need to be re-run. Steps is provided to ensure that only
 * invocations that correspond to steps that are still in the manifest are run.
 *
 * The function returns a list of build step hashes that can be found as keys in
 * invocations.entries.
 */
std::vector<Hash> dirtyInvocations(
    FileSystem &file_system,
    const std::unordered_set<Hash> &step_hashes,
    const Invocations &invocations);

/**
 * Find build steps that don't have a corresponding entry in the invocation log.
 * These steps are dirty and should be run.
 */
std::vector<Step> unbuiltSteps(
    const std::unordered_set<Hash> &step_hashes,
    const Invocations &invocations);

/**
 * Compute the tasks that need to be performed to do the build.
 */
Tasks tasks(
    const Invocations &dirty_invocations,
    const std::vector<Step> &unbuilt_steps);

/**
 * Delete the provided outputs, and also the containing folders that Shuriken
 * have created.
 */
void deleteOutputs(
    FileSystem &file_system,
    const std::vector<Fingerprint> &outputs);

/**
 * Perform build tasks.
 */
void executeTasks(
    CommandRunner &command_runner,
    FileSystem &file_system,
    BuildStatus &build_status,
    InvocationLog &invocation_log,
    const Tasks &tasks);

/**
 * Perform a build.
 */
void build(
    FileSystem &file_system,
    CommandRunner &command_runner,
    BuildStatus &build_status,
    InvocationLog &invocation_log,
    const std::vector<Step> &steps,
    const Invocations &invocations);

}  // namespace shk
