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
    "clean-entry": "Open a clean repository and start a task from the new-task form",
    "navigation": "Reach every screen with 1-4; Tab/Shift-Tab move between panes and Esc returns the focus",
    "attachment": "Attach, type to the agent, resize and return without stopping it",
    "removal": "Remove a head in-app, confirmed in a dialog, with dirty-work protection",
    "planning": "Converse to a proposal, revise it, approve the exact revision in one dialog; checked delivery",
    "result-review": "Find a finished run's Run page, review its verdict, diff, requirements and verify "
    "command in the Inbox; verifier retired, worker grouped under its run with land/dismiss",
    "recovery": "A failed repository check is explained on the Run page, in the Inbox and the review; "
    "the planning agent's corrected revision is approved in one dialog and succeeds",
    "recovery-terminal": "An interactive head's missing terminal is explained without "
    "inferring failure or loss, and the offered action restores access to the work",
}
