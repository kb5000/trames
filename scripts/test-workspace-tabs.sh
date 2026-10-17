#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Same Foundation-only model/XCTest cases as the app, without UIKit substitutes.
set -euo pipefail
cd "$(dirname "$0")/.."
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
python3 - "$stage/main.swift" <<'PY'
import pathlib,re,sys
source=pathlib.Path('apps/ios/TramesTests/WorkspaceTabsTests.swift').read_text()
names=re.findall(r'func (test\w+)\(',source)
pathlib.Path(sys.argv[1]).write_text('import XCTest\nXCTMain([testCase([\n'+',\n'.join(f'("{n}", WorkspaceTabsTests.{n})' for n in names)+'\n])])\n')
PY
"${SWIFTC:-swiftc}" -module-cache-path "$stage/module-cache" apps/ios/Trames/WorkspaceTabs.swift apps/ios/TramesTests/WorkspaceTabsTests.swift "$stage/main.swift" -o "$stage/workspace-tests"
"$stage/workspace-tests"
