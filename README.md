# ARTestCLI x64

A C++17 command-line tool for automating test sequences using a plugin-based instrument architecture.  
✅ Built for **x64 architecture**.

## ✨ Features
- Modular design with dynamically loaded instruments (e.g., power supplies, relay cards, CAN devices, etc.)
- JSON-based test sequence configuration
- Command-line interface for flexible execution and debugging

## ⚙️ Build

### Using Visual Studio
- Open the solution `ARTestCLI.sln`
- Select **x64** as the target architecture
- Choose Release or Debug configuration
- Build the project

## 🚀 Usage

Run the following commands from the output directory (`Release` or `Debug`):

```bash
run Scripts/TestInstruments.json
compile Scripts/TestInstruments.json
debug Scripts/TestInstruments.json
break Scripts/TestInstruments.json 1,3