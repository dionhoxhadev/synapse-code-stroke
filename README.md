# 🧠 Synapse

**A stroke command center that runs in your terminal, written in plain C++.**

Synapse follows one stroke patient from the moment the ambulance arrives to the recovery clinic weeks later. It maps the brain's blood supply, works out what a blocked pipe shuts down, scores the patient, races the clock to the right hospital, replays a busy morning in the stroke unit and checks the brain again on the way back to normal.

`C++11` · `no outside libraries` · `Code::Blocks or g++` · `tested on Windows`

> ⚠️ This is a learning project. The medical data is simplified and partly made up. Never use it for real patients.

---

## ✨ What it can do

```text
synapse> triage new
Patient name: Arben Hoxha
Age: 67
...
Severity:             8 (moderate)
Likely blocked pipe:  left-middle (big)
Drug possible:        yes, until 12:00 (3 h 50 min left)
Clot removal:         yes, until 07:30 tomorrow

synapse> clinic results P-0001
Arben Hoxha, 67 - normal for ages 65-79: reaction 390 ms, memory 6.0 digits, colour words 18.0 s

DAY   REACTION              MEMORY                COLOUR WORDS
14    540 ms well below     4 digits below        31.0 s well below
45    470 ms below          5 digits normal       26.0 s below
90    430 ms normal         6 digits normal       22.0 s normal

Trend, day 90 against day 45: reaction improving, memory improving, colour words improving
```

| Stage | What happens | The idea behind it |
|---|---|---|
| 🗺️ **Atlas** | Loads the brain's pipes and areas and draws them as a tree | Graph, DFS, recursion |
| 🩸 **Lesion** | Blocks a pipe and shows which areas lose blood | Breadth-first search |
| 🚑 **Triage** | Scores the patient, guesses the blocked pipe, checks the time limits | Rules + its own JSON database |
| 🛣️ **Route** | Finds the fastest hospital that can treat the patient | Dijkstra's algorithm |
| 🏥 **Stroke unit** | Replays a morning of scans, drugs and clot removals | Event simulation with queues |
| 🧪 **Clinic** | Runs three brain tests and compares them with healthy people the same age | Base class + virtual functions |
| 📁 **Case file** | Tells one patient's whole story and estimates the brain cells lost | Search and reporting |

---

## 📂 The versions

The project was built one stage at a time, and every stage is kept as its own folder. Each one is a complete program. *Pjesa* means *part* in Albanian.

| Folder | Adds | New files |
|---|---|---|
| `Synapse v.1/Pjesa 1` | Atlas | `atlas`, `synapse` |
| `synapse v.2/Pjesa 2` | Lesion, bridges between pipes | `lesion` |
| `synapse v.3/Pjesa 3` | Triage and saved cases | `triage`, `cases`, `json` |
| `synapse v.4/Pjesa 4` | Hospital routes and transfer plans | `citymap`, `router` |
| `synapse v.5/Pjesa 5` | Stroke unit simulation | `unit` |
| `synapse v.6/Pjesa 6` | Recovery clinic with three brain tests | `clinic`, `braintest` |
| `synapse v.7/Pjesa 7` | Patient case file | `casefile` |

👉 **v.6 is the most complete version.** v.7 grows from v.5, so it has the case file but not the clinic.

---

## 🚀 Run it

### Code::Blocks

1. Open `synapse.cbp` from the version folder you want. Open the folder itself, not a zip.
2. Click **Build → Rebuild**, then **Build → Run**.

### g++

```bash
cd "synapse v.6/Pjesa 6"
g++ -std=c++11 -Wall *.cpp -o synapse
./synapse
```

Run it from the version folder, so it finds the `data` folder. Type `help` to see every command.

---

## ⌨️ Commands

| Command | What it does |
|---|---|
| `atlas pipes` · `atlas areas` | List every pipe or area |
| `atlas show <name>` | Show one pipe or area in full |
| `atlas tree` | Draw the network from the heart |
| `atlas path <area>` | Every route from the heart to an area |
| `lesion block <pipes>` | What stops working if these pipes are blocked |
| `lesion risk <area>` | Every pipe that alone would stop an area |
| `triage new` | Create a patient case |
| `triage update <id>` | Add new symptoms |
| `triage urgent` | Cases that can still be treated |
| `route from <city>` | Every hospital, fastest first |
| `route <id>` | Choose and save a hospital for a patient |
| `unit run <file>` | Replay a morning (`--order first\|deadline`, `--scanners <n>`) |
| `unit compare <file>` | One scanner against two |
| `clinic visit <id>` | Play the three brain tests and save the visit |
| `clinic results <id>` | Every visit, rated for the patient's age |
| `clinic check <id>` | Flag results the damage does not explain |
| `clinic chart <id> <test>` | Draw a test as bars |
| `case <id>` · `case find <text>` | One patient's whole story, or a search |
| `case cost <id>` | Minutes and brain cells lost before treatment |

---

## 🧱 How the code is built

```text
synapse.cpp   the Shell: reads commands and prints
engines       atlas, lesion, triage, cases, citymap, router, unit, clinic ...
data/         plain text files, plus cases.json written while it runs
```

- **The engines never print.** They only calculate, and `Shell` decides what to show.
- **Files load all or nothing.** A mistake prints its line number, and the old data stays in use.
- **Saving cannot corrupt the database.** It writes to a temporary file first, then swaps it in.
- **Every rule number has a name**, such as `DRUG_WINDOW` or `SCAN_MINUTES`, kept in one header.
- **Times are minutes from midnight.** Yesterday is negative and tomorrow is 1440 or more, so times add and compare as plain numbers.
- **No outside libraries.** Even the JSON reader is written by hand.

### Data files

Everything is plain text, one record per line, separated by `|`. Lines starting with `#` are comments.

```text
PIPE | left-middle-top | small | left-middle
AREA | speaking        | left-middle-top | cannot-speak | Forms words and sentences
```

---

## 🧪 Testing

Stages 2 to 6 were checked against independent Python models, with thousands of random cases and zero differences. Every version compiles under C++11, 14, 17 and 20. The brain tests were also played by a script that waits for the `*`, reads the digits and the colours, and presses keys like a person would.

---

## 👤 Author

**Dion Hoxha** · [github.com/dionhoxhadev](https://github.com/dionhoxhadev)

<sub>Started as my project for Albanian Skills 2026, Software Development.</sub>
