# Results backend — design mockups

Design mockups for **pb#295**, the optional results-tracking backend shared by Paperboat,
Squero OM and future Studio apps. They are proposals, not screenshots of a built feature.

| Image | Shows |
| --- | --- |
| [`01-entrypoints.jpg`](01-entrypoints.jpg) | The four entrypoints (Paperboat Board, Squero OM Simulate, CLI/CI, future Studio apps), the one-time connect dialog, and the shared record sink. |
| [`02-record.jpg`](02-record.jpg) | What `record.json` holds: run, settings, tool versions, one entry per block with logs and named values, outputs/artifacts, timeline. |
| [`03-report.jpg`](03-report.jpg) | An A4 run report generated from the record: summary, variables, block activity, signals, artifacts and provenance. |
| [`04-uses.jpg`](04-uses.jpg) | What the record is for: report, compare, audit, re-run, search, ELN, regression, export. |

The record is the contract; the results service is an optional external consumer. The report
and the notebook are later layers, not part of the first results-tracking slice.
