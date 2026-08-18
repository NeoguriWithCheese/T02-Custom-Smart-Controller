> [!NOTE]
> 🇰🇷 한국어 유저이신가요? [여기를 눌러 한국어 설명서(Korean Version)를 확인하세요!](README_ko.md)

# T02 Custom Smart Controller (ESP32) 🚀

This is an **open-source, highly optimized ESP32 custom controller firmware** designed specifically for the heavy-duty **T02 Sex Machine (120W, 24V Turbo Gear Motor)**. 

It provides ultra-smooth, whisper-quiet motor control and seamless BLE compatibility with popular mobile apps.

---

## 🚨 LEGAL DISCLAIMER & WARNING (필독 / 법적 고지)

> [!CAUTION]
> **HIGH POWER ELECTRONICS INVOLVED!** 
> This project deals with 24V high-current power supplies and a 120W industrial-grade motor. Incorrect wiring can result in **short circuits, fire, property damage, or severe bodily injury**.
> * **AS IS:** This guide and software are provided "AS IS", without warranty of any kind. 
> * **NO LIABILITY:** The author assumes absolutely NO liability for any damages, fires, or injuries caused by assembling or using this device. **You build and use this entirely at your own risk.**
> * **UNOFFICIAL:** This is a 100% unofficial, community-driven DIY project. It is **NOT** affiliated with, endorsed by, or sponsored by Hismith, Lovense, or any other brand. Brand names are used purely for app interoperability context.

---

## 🛠️ Hardware Requirements

* **Microcontroller:** ESP32 (e.g., ESP32-WROOM-32)
* **Motor Driver:** BTS7960 (43A High Power Driver)
* **Step-Down Module:** LM2596 (To convert 24V to 5V for the ESP32)
* **Power Supply:** 24V 5A~6A DC Adapter
* **Inputs:** 100K Potentiometer (Dial), Push Button (Mode Switch)
* **Motor:** 24V 120W Turbo Gear Motor (Standard T02 Motor)

---

## ⚙️ Critical Hardware Preparation

> [!CAUTION]
> **🔥 1. WIRE GAUGE (Fire Hazard):** For the thick lines (Red and Blue) in the diagram representing the **24V power and motor output lines, you MUST use thick wires of 16 AWG or thicker!** Using thin breadboard jumper wires will cause them to melt under the high current (up to 10A) and start a fire.
> 
> **⚡ 2. 5V STEP-DOWN SETTING (Board Destruction):** Before connecting the LM2596 module to the ESP32, you **MUST use a multimeter and turn the screw to set the output voltage to exactly 5V**. If you connect it straight out of the box (which defaults to 24V), it will instantly DESTROY your ESP32 board!
> 
> **🛡️ 3. METAL CHASSIS SHORT CIRCUIT:** When placing the components inside the T02's metal controller box, if the soldered pins on the boards touch the metal chassis, it will cause a catastrophic short circuit. **You MUST wrap the boards in electrical tape or use standoffs to ensure they NEVER touch the metal casing.**

---

## ⚡ Wiring Diagram (배선도)

Just follow the labels printed on your boards. Connect the matching names!

```mermaid
%%{init: {"flowchart": {"curve": "step", "nodeSpacing": 80, "rankSpacing": 120}}}%%
graph LR
    Power[24V Power]
    StepDown[LM2596]
    ESP["E<br>S<br>P<br>3<br>2"]
    Pot[Wired Controller]
    Btn[Push Button]
    StatusLED[Status LED]
    BTS[BTS7960]
    Motor((T02 Motor))

    Power ===|"24V → LED"| Pot
    Power ===|"GND → LED"| Pot

    Power ===|"24V → IN+"| StepDown
    Power ===|"GND → IN-"| StepDown

    Power ===|"24V → B+"| BTS
    Power ===|"GND → B-"| BTS

    StepDown ---|"OUT+ → VCC"| ESP
    StepDown ---|"OUT- → GND"| ESP

    ESP ---|"3.3V"| Pot
    ESP ---|"GND"| Pot
    Pot ---|"GPIO 32"| ESP

    ESP ---|"GND"| Btn
    Btn ---|"GPIO 33"| ESP

    ESP ---|"GPIO 2"| StatusLED
    ESP ---|"GND"| StatusLED

    ESP ---|"VCC → VCC"| BTS
    ESP ---|"GND → GND"| BTS
    ESP ---|"GPIO 25 → RPWM"| BTS
    ESP ---|"GPIO 26 → LPWM"| BTS
    ESP ---|"GPIO 27 → R_EN"| BTS
    ESP ---|"GPIO 14 → L_EN"| BTS

    BTS ===|"M+"| Motor
    BTS ===|"M-"| Motor

    linkStyle 0,2,4 stroke:#e74c3c,stroke-width:4px,color:#c0392b;
    linkStyle 1,3,5 stroke:#2980b9,stroke-width:4px,color:#2980b9;
    linkStyle 6,8,15 stroke:#e67e22,stroke-width:2px,color:#d35400;
    linkStyle 7,9,11,14,16 stroke:#2c3e50,stroke-width:2px,color:#2c3e50;
    linkStyle 10,12,13,17,18,19,20 stroke:#27ae60,stroke-width:2px,color:#1e8449;
    linkStyle 21 stroke:#e74c3c,stroke-width:6px,color:#c0392b;
    linkStyle 22 stroke:#2980b9,stroke-width:6px,color:#2980b9;
```

### 🔌 5-Pin Aviation Connector (For T02 Stock Remote)

If you are using a 5-pin aviation jack (GX16) to connect the stock T02 remote (which includes a 24V LED), follow this pinout. (Looking at the solder cups on the back of the connector)

<div align="center">
  <table style="text-align: center; border: none; font-size: 1.1em; width: 500px; margin: auto;">
    <tr>
      <td width="33%" style="border: none;"></td>
      <td width="34%" style="border: none; padding-bottom: 20px;"><b>[ Top Notch (Keyway) ]<br>⬇️</b></td>
      <td width="33%" style="border: none;"></td>
    </tr>
    <tr>
      <td style="border: none;"><h1>⑤</h1><b style="color:#e74c3c;">(24V LED +)</b></td>
      <td style="border: none;"></td>
      <td style="border: none;"><h1>①</h1><b style="color:#e67e22;">(3.3V VCC)</b></td>
    </tr>
    <tr>
      <td style="border: none;"><br><h1>④</h1><b style="color:#34495e;">(24V LED -)</b><br>Adapter GND</td>
      <td style="border: none;"></td>
      <td style="border: none;"><br><h1>②</h1><b style="color:#27ae60;">(GPIO 32)</b><br>Dial Signal</td>
    </tr>
    <tr>
      <td style="border: none;"></td>
      <td style="border: none;"><br><h1>③</h1><b style="color:#000;">(ESP32 GND)</b><br>ESP32 GND</td>
      <td style="border: none;"></td>
    </tr>
  </table>
</div>

> [!WARNING]
> **EXPLOSION HAZARD:** If Pin ⑤ (24V) accidentally shorts with Pin ① (3.3V) or Pin ② (GPIO), 24V will shoot directly into the ESP32, causing it to instantly explode! You MUST use heat shrink tubing on every single pin after soldering.

---

### Pin Mapping Summary
| From | To |
| :--- | :--- |
| Adapter **24V+** | BTS7960 **B+** / LM2596 **IN+** |
| Adapter **24V-** | BTS7960 **B-** / LM2596 **IN-** |
| Wired Dial **Signal** | ESP32 **GPIO 32** |
| Wired Button **Signal** | ESP32 **GPIO 33** |
| ESP32 **GPIO 2** | Status LED **(+)** |
| ESP32 **GND** | Status LED **(-)** |
| ESP32 **GPIO 25** | BTS7960 **RPWM** |
| ESP32 **GPIO 26** | BTS7960 **LPWM** |
| ESP32 **GPIO 27** | BTS7960 **R_EN** |
| ESP32 **GPIO 14** | BTS7960 **L_EN** |

> [!IMPORTANT]
> **Ground Loop Warning:** ALL `GND` (Minus) pins on the ESP32, Step-down module, and BTS7960 control logic must be tied together. DO NOT mix up the heavy 24V motor minus (`M-`) with logic GND!


## 💻 Firmware Installation & Usage

1. Open `T02_Custom_Controller.ino` in the Arduino IDE.
2. Install the **NimBLE-Arduino** library from the Library Manager.
3. Select your ESP32 board and compile (Ensure you are using ESP32 Arduino Core v3.x+).
4. **Operation Modes:**
   - Press the mode button to cycle through modes.
   - 🔵 **1 Blink:** Manual Dial Mode
   - 🔵🔵 **2 Blinks:** Lovense App Mode
   - 🔵🔵🔵 **3 Blinks:** Hismith App Mode

## 📜 License
This project is licensed under the **MIT License**. Feel free to use, modify, and distribute, provided you include the original copyright notice and disclaimers.
