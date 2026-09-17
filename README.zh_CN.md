# fcitx5-tmuxpass

[English](README.md) | 中文

fcitx5 官方 addon 机制的独立小插件（一个 .so，不改 fcitx5 本体）。解决：中文输入法激活时，
tmux / screen / zellij / emacs（`C-x`）等前缀键工具的后续按键会被输入法吃进候选框，
每次都得先切英文。装上后，按下 prefix 键即自动执行配置的动作，无需手动切换。

## 两种动作模式（Action，GUI 下拉框可切）

| 模式 | 行为 | 回中文方式 |
| --- | --- | --- |
| `SwitchIM`（默认） | prefix 按下瞬间把当前窗口切到第一个输入法（keyboard-us）。fcitx5 进程内同步生效，无时间窗竞态 | 按你平时的切换键（如 Ctrl+Space）手动切回——切出去全自动，切回来由你表达意图 |
| `PassThrough` | prefix 后时间窗（默认 400ms，每键刷新）内按键跳过输入法引擎、原样透传给应用（v0.1 旧逻辑，保留为可选） | 无需切换，窗口超时自动恢复 |

默认 SwitchIM 的原因：时间窗两难——短了漏键进候选框，长了 prefix 后正常中文输入的前几
个字符被裸透传成 ASCII。切换输入法两个方向都是确定性的。

## 原理

在 `PreInputMethod` 阶段监听全局按键事件（`Instance::watchEvent`），先于任何输入法引擎：

- SwitchIM：prefix 按下 → `Instance::deactivate()`（与手动切英文同路径，作用于最后聚焦
  IC）；后续命令键自然经英文键盘直达应用
- PassThrough：窗口内非 modifier 按键调用 `KeyEvent::filter()`（只 filter 不 accept）：
  引擎收不到该键，frontend 把原始按键发回应用（wayland 的 `sendKeyToVK` / dbus 返回未
  处理）——真正的透传，不是模拟输入

对所有 frontend（wayland_v2 / dbus / xim）和所有输入法引擎（rime、pinyin 等）生效。

## 与现有方案对比

| 方案 | 层级 | 每次 prefix 起进程 | 要改工具配置 | 超出 tmux 可用 | GUI 可配 |
| --- | --- | --- | --- | --- | --- |
| 本插件（SwitchIM） | fcitx5 进程内 | 否 | 否 | 是 | 是 |
| tmux `run-shell` + `fcitx5-remote`（博客偏方） | tmux | 是 | 是（重绑 prefix） | 否 | 否 |
| macOS `macism` + tmux 根表绑定 | tmux + macOS | 是 | 是（禁默认 prefix） | 否 | 否 |
| vim autocmd + `fcitx5-remote` | 编辑器 | 是 | 是（每个编辑器配） | 否 | 否 |

tmux 层的 hack 还要禁用默认 prefix、用 `switch-client -T` 手动进 prefix 表，各有边角问
题；fcitx5 内的 addon 在任何工具消费按键之前就能看到它。

## 编译与安装

依赖：`fcitx5`（Arch 包内已含全部头文件）、C++20 编译器。

```bash
make
sudo make install    # libtmuxpass.so -> /usr/lib/fcitx5，conf -> /usr/share/fcitx5/addon
fcitx5 -r            # 重启 fcitx5 生效
```

**预编译成品**（fcitx5 5.1.x，x86_64）：到 [Releases](https://github.com/tomasWade/fcitx5-tmuxpass/releases) 页下载 `fcitx5-tmuxpass-<版本>-linux-x86_64.tar.gz`：

```bash
tar -xzf fcitx5-tmuxpass-*-linux-x86_64.tar.gz
sudo install -Dm755 libtmuxpass.so -t /usr/lib/fcitx5/
sudo install -Dm644 tmuxpass.conf -t /usr/share/fcitx5/addon/
fcitx5 -r
```

预编译包为 strip 后成品，匹配 fcitx5 5.1.x ABI。若你的 fcitx5 版本加载失败或架构不同，改用源码编译（release 页也有自动生成的源码 tarball）——对着实际运行的 fcitx5 编译永远最可靠。

卸载：`sudo make uninstall && fcitx5 -r`。

## 配置

`~/.config/fcitx5/conf/tmuxpass.conf`（手改后 `fcitx5 -r`，或用 fcitx5-configtool 的
GUI 配置页即时生效）：

```ini
[PrefixKeys]
0=Control+q
# 可加多行，如 1=Control+b

Action=SwitchIM

[Programs]
0=foot
# 可加多行，如 1=kitty、2=footclient；整节删除 = 全局生效

WindowMs=400
```

| 配置项 | 默认 | 说明 |
| --- | --- | --- |
| `PrefixKeys` | `Control+q` | prefix 键列表，标准 fcitx 键名格式，可多个 |
| `Action` | `SwitchIM` | prefix 后动作：`SwitchIM` 切英文 / `PassThrough` 时间窗透传 |
| `Programs` | `foot` | 生效程序 appId 列表；空列表 = 全局生效 |
| `WindowMs` | `400` | 透传时间窗（毫秒），范围 100–5000，仅 `PassThrough` 模式使用 |

## 验证

SwitchIM 模式：

1. rime 中文状态，foot 内 `Ctrl+Q c`：tmux 直接新建窗口，不弹候选框；`fcitx5-remote`
   显示 1（inactive）
2. `Ctrl+Q [` 进 copy-mode 翻页正常；按 Ctrl+Space 回中文
3. 列表外程序（如 Firefox，Ctrl+Q 是退出键）按 Ctrl+Q：输入法状态不变
4. 平时中文输入不受影响

PassThrough 模式：

5. `Ctrl+Q [` 后 400ms 内连续 `j/k`：copy-mode 翻页正常，窗口外按键正常进输入法
6. Programs 过滤对两种模式同等生效；列表清空后回到全局行为

## FAQ

**怎么查终端的 appId？** `fcitx5-diagnose` 的 input context 列表 Program 列，或终端设
置的 wayland appId（foot → `foot`，kitty → `kitty`，footclient 窗口仍是 `foot`）。

**为什么切到第一个输入法？** fcitx5 的 deactivate 就是切到输入法组第一项。确保
fcitx5-configtool 里输入法列表第一项是纯键盘布局（如 `keyboard-us`）——默认即如此。

**影响其他窗口吗？** fcitx5 默认按程序保存输入法状态，切换只作用于当前应用。

**升级：** fcitx5 大版本升级（ABI 变化）后重新 `make && sudo make install`。

## 许可

LGPL-2.1-or-later，与 fcitx5 一致，见 [LICENSE](LICENSE)。
