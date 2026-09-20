#pragma once

// RigRoom --plugin-snapshot <plugin-uri> <out.png> [settle-ms]
// Opens the plugin's own GUI in this process and writes a PNG of it.
int runPluginSnapshot(int argc, char* argv[]);

// RigRoom --plugin-snapshot-batch <queue.txt> <out-dir>
// Runs one snapshot process per plugin listed in the queue and prints a line
// per plugin ("PREVIEW OK <uri>" / "PREVIEW FAIL <uri> <reason>"). Meant to be
// started inside a hidden display by PluginPreviewService.
int runPluginSnapshotBatch(int argc, char* argv[]);
