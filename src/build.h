#pragma once

#include "build_status.h"
#include "command_runner.h"
#include "file_system.h"
#include "invocation.h"
#include "step.h"

namespace shk {

/**
 * Find build invocations that are in the invocation log but no longer exist in
 * the Manifest graph. Their outputs should be deleted.
 */
Invocations staleInvocations(
    const Steps &steps,
    const Invocations &invocations);

/**
 * Find invocations that need to be re-run. Steps is provided to ensure that only
 * invocations that correspond to steps that are still in the manifest are run.
 */
Invocations dirtyInvocations(
    FileSystem &file_system,
    const Steps &steps,
    const Invocations &invocations);

/**
 * Find build steps that don't have a corresponding entry in the invocation log.
 * These steps are dirty and should be run.
 */
std::vector<Step> unbuiltSteps(
    const Steps &steps,
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
    const Steps &steps,
    const Invocations &invocations);

}  // namespace shk
