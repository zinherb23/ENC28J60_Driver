🌐 ENC28J60_Driver
An ENC28J60 driver for the STM32F401RE

🛠️ Tech Stack & Dependencies 
Language: C11
Cross-Compiler: arm-none-eabi-gcc
Hardware Interface: STM32F4xx HAL / CMSIS

📁 Project Structure
```text
├── Core/
│   ├── Inc/
│   │   ├── enc.h                      # module header
│   │   ├── main.h                     
│   │   ├── stm32f4xx_hal_conf.h
│   │   └── stm32f4xx_it.h
│   │
│   ├── Src/
│   │   ├── enc.c                      # module logic
│   │   ├── main.c                     # entry point
│   │   ├── stm32f4xx_hal_msp.c
│   │   ├── stm32f4xx_it.c
│   │   ├── syscalls.c
│   │   ├── sysmem.c
│   │   └── system_stm32f4xx.c 
│   │
│   └── Startup/
│ 
└── Drivers/
