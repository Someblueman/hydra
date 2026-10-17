"""Scripted journeys by name, with the purpose each one states in the report."""

from __future__ import annotations

from usability.journeys_core import attachment, clean_entry, navigation, recovery_terminal, removal
from usability.journeys_plan import planning
from usability.journeys_review import recovery, result_review

JOURNEYS = {
    "clean-entry": clean_entry,
    "navigation": navigation,
    "attachment": attachment,
    "removal": removal,
    "planning": planning,
    "result-review": result_review,
    "recovery": recovery,
    "recovery-terminal": recovery_terminal,
}

PURPOSE = {
    "clean-entry": "Open a clean repository and start a task from visible controls",
    "navigation": "Reach every view with number keys, Tab/Shift-Tab and Esc",
    "attachment": "Attach, type to the agent, resize and return without stopping it",
    "removal": "Remove a head in-app with dirty-work protection",
    "planning": "Converse, revise, validate and approve a plan; checked delivery",
    "result-review": "Find a finished run in Overview, review its verdict, diff, "
    "requirements and verify command; verifier retired, worker grouped with land/dismiss",
    "recovery": "A failed repository check is explained in the run panel, Attention and "
    "review; the planning agent's corrected revision is validated, executed with y and succeeds",
    "recovery-terminal": "An interactive head's missing terminal is explained without "
    "inferring failure or loss, and the offered action restores access to the work",
}
