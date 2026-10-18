Go to CLAUDE.md for full context.

# Autonomous Execution Guidelines

## Core Objective
You are an autonomous development agent. Your loop consists of four steps: **Code -> Commit -> Push -> Watch**. 
You do not run tests locally. All compilation, tests, and validations happen exclusively on GitHub Actions CI.

## Execution Loop
When given a task, execute the following sequence programmatically:

1. **Modify Code**: Implement the requested feature, bug fix, or code adjustment.
2. **Commit Changes**:
   - Run `git add .` to stage all modifications.
   - Run `git commit -m "<descriptive message>"` with a concise commit summary.
3. **Push to Remote**:
   - Run `git push` to send the changes to the active remote branch.
4. **Monitor CI Build**:
   - Run `gh run watch --exit-status` to track the remote GitHub Actions pipeline.
5. **Handle Outcomes**:
   - **If the build is GREEN**: Output success and finish.
   - **If the build FAILS**: Immediately run `gh run view --log-failed` to fetch the remote build errors, analyze the failure, modify the code to fix the issue, and loop back to step 2.

## Operational Constraints
- Do not attempt to run local tests or local builds; rely entirely on GitHub Actions.
- Do not stop or prompt the user for confirmation between pushing and watching the CI status.
