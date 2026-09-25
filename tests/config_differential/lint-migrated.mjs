// Runs core's canonical config lint over config/FarCry6HeadTracking.ini and over every
// distinct file the differential test migrated (the folder it names as the argument).
//
// A migrated file keeps a player's rebound hotkeys, which the lint reports as differing
// from the fleet's defaults: that rule is for the committed file, so it is the one problem
// a migrated file may have.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { lintCanonicalConfig } from "../../cameraunlock-core/scripts/check-canonical-config.mjs";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const migratedDir = process.argv[2];
if (!migratedDir) throw new Error("usage: node lint-migrated.mjs <folder of migrated files>");

const options = { dialect: "native", exceptions: undefined };
const REBOUND = /differs from the fleet's /;
const failures = [];

const committed = path.join(repo, "config", "FarCry6HeadTracking.ini");
for (const problem of lintCanonicalConfig(fs.readFileSync(committed), options)) {
  failures.push(`config/FarCry6HeadTracking.ini: ${problem}`);
}

const files = fs.readdirSync(migratedDir).filter((f) => f.endsWith(".ini"));
if (files.length === 0) throw new Error(`${migratedDir} holds no migrated files`);
for (const file of files) {
  for (const problem of lintCanonicalConfig(fs.readFileSync(path.join(migratedDir, file)), options)) {
    if (!REBOUND.test(problem)) failures.push(`${file}: ${problem}`);
  }
}

if (failures.length > 0) {
  for (const f of failures) console.log(`FAIL ${f}`);
  process.exit(1);
}
console.log(`canonical config lint: the committed file and ${files.length} migrated files pass`);
