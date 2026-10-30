# gpio_probe — 墨水屏接线电平观察

独立诊断工程，不驱动屏幕，只把六个引脚配置为输入并每秒打印电平。

| 信号 | GPIO |
| --- | --- |
| DIN (MOSI) | 7 |
| CLK (SCLK) | 6 |
| CS | 10 |
| DC | 3 |
| RST | 4 |
| BUSY | 5 |

## 构建 / 烧录（PowerShell）

```powershell
. G:\tools\workbuddy\esp32\tools\idf-env.ps1
cd G:\tools\workbuddy\esp32\tools\gpio_probe
idf.py set-target esp32c3   # 仅首次
idf.py build
idf.py -p COM4 flash
idf.py -p COM4 monitor      # 退出：Ctrl+]
```

从仓库根目录也可以：`idf.py -C tools/gpio_probe build`。

## 判读

- `levels=0x2A` 之类的掩码：bit0..bit5 依次对应 DIN、CLK、CS、DC、RST、BUSY。
- 某位稳定为固定值：该线有确定电平（外部驱动、屏板输出或外部上下拉）。
- 某位每秒随机跳变：引脚悬空（无外部驱动且内部上下拉已禁用）。
- `level change` 行只在该秒电平相对上一秒变化时出现。
