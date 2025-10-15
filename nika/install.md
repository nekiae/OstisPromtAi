# NIKA — Ubuntu install and run guide (Prompt Assistant ready)

This guide was verified for Ubuntu 22.04+. It includes steps needed for the Prompt Assistant (Wit.ai) integration.

## 1) Prerequisites
- Git, curl, tar
- Python 3.10+, venv, pip
- Node.js + npm (for React interface)

Install dependencies:
```bash
sudo apt update
sudo apt install -y git curl tar python3 python3-venv python3-pip nodejs npm
```

(Optional) Use Node.js LTS from NodeSource if your distro Node is too old.

## 2) Clone repository
```bash
git clone https://github.com/nekiae/OstisPromtAi
cd OstisPromtAi/nika
# If the project uses submodules in your branch
git submodule update --init --recursive
```

## 3) Install C++ problem solver toolchain
Downloads prebuilt sc-machine, scl-machine and nika solver into `install/`.
```bash
./scripts/install_cxx_problem_solver.sh
```

## 4) Configure Wit.ai (Prompt Assistant)
Edit `nika.ini` and set your Wit.ai server token:
```ini
[wit-ai]
server_token = <YOUR_WIT_SERVER_TOKEN>
url = https://api.wit.ai/message
```
Notes:
- Internet access is required at runtime (Python solver uses Wit.ai).
- The Prompt Assistant maps Wit entities to KB relations: `rrel_role`→`nrel_role`, `rrel_domain`→`nrel_domain`, `rrel_task`→`nrel_task`.
- Rebuild the KB after any KB changes.

## 5) Build knowledge base
```bash
./scripts/start.sh build_kb
```
This creates `kb.bin` using `repo.path`.

## 6) Run services (each in its own terminal)

### 6.1 C++ problem solver (sc-machine)
```bash
./scripts/start.sh machine
```
- Uses `nika.ini` and `kb.bin`.
- Exposes sc-server on `127.0.0.1:8090` by default.

### 6.2 sc-web UI
If `sc-web/` is empty, clone it first:
```bash
# From project root
git clone https://github.com/ostis-ai/sc-web sc-web
```
Create a dedicated virtualenv and install dependencies:
```bash
cd sc-web
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cd ..
```
Run sc-web via helper script:
```bash
./scripts/start.sh web
```

### 6.3 Python problem solver
Create a virtualenv and install dependencies:
```bash
python3 -m venv problem-solver/py/.venv
source problem-solver/py/.venv/bin/activate
pip install -r problem-solver/py/requirements.txt
```
Run the server:
```bash
./scripts/start.sh py_server
```

### 6.4 React interface
```bash
cd interface
npm install
npm run build
cd ..
./scripts/start.sh interface
```

## 7) What changed with Prompt Assistant
- No new services or processes were added; existing run flow stays the same.
- New KB section: `knowledge-base/extra/section_prompt_assistant/`.
- Ensure `nika.ini` has a valid `[wit-ai]` `server_token`.
- Relations now include Wit.ai mapping: `nrel_role/domain/task` have `nrel_wit_ai_idtf` set to `rrel_role/domain/task` for entity alignment.

## 8) Troubleshooting
- sc-web folder is empty: clone `https://github.com/ostis-ai/sc-web` into `sc-web/` and install Python deps as above.
- After KB edits: re-run `./scripts/start.sh build_kb`.
- Ports: sc-server `127.0.0.1:8090` (see `[sc-server]` in `nika.ini`).
- Logs: see `logs/sc-server.log` and console output.
