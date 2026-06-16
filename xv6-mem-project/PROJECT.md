# xv6 核心動態記憶體管理子系統

本專題以 [xv6-riscv](https://github.com/mit-pdos/xv6-riscv)（MIT 教學用 UNIX 作業系統）為基礎，
在核心空間中實作一套支援 **First-Fit / Best-Fit / Worst-Fit** 三種配置策略、具備
**相鄰空洞合併（coalescing）** 的動態記憶體配置子系統，並提供系統呼叫讓使用者程式
切換策略與讀取配置器統計資訊，用於碎片化分析與延遲量測。

## 目錄結構（相對於 xv6 原始碼樹）

- `kernel/kmalloc.h` — 共用定義：策略常數（`KM_FIRST_FIT` / `KM_BEST_FIT` / `KM_WORST_FIT`）
  與 `struct kmstat`（配置器統計快照）。
- `kernel/kmalloc.c` — 配置器本體：
  - 在核心 BSS 區段內保留一塊 512KB 連續記憶體作為 heap（`km_heap`），確保管理範圍
    一定是連續實體記憶體，不依賴 `kalloc()` 的單頁配置。
  - 以「依位址排序的雙向鏈結串列」管理所有區塊（已配置 / 空閒），每個區塊前方有一個
    `struct kmblock` 標頭（大小、free flag、prev/next）。
  - `kmalloc(size)`：依目前策略搜尋空閒區塊（First-Fit 找到第一個就用；Best-Fit /
    Worst-Fit 走訪整個串列找最小 / 最大可用區塊），若剩餘空間夠大則切割出新的空閒
    區塊。
  - `kmfree(ptr)`：將區塊標記為 free，並嘗試與前後相鄰的空閒區塊合併
    （coalescing），降低外部碎片化。
  - `kmsetpolicy(policy)` / `kmgetstat(struct kmstat*)`：切換策略、回傳統計資訊
    （heap 大小、已用/空閒位元組數、已用/空閒區塊數、最大空閒區塊）。
- `kernel/sysproc.c` — 新增四個系統呼叫的實作：`sys_kmalloc`、`sys_kmfree`、
  `sys_kmpolicy`、`sys_kmstat`。
- `kernel/syscall.h`、`kernel/syscall.c` — 註冊新系統呼叫編號 22–25。
- `user/usys.pl`、`user/user.h` — 使用者空間的系統呼叫進入點與宣告。
- `user/kmtest.c` — 使用者空間測試程式（對應提案「四、系統效能評估與驗證指標」）：
  - 對三種策略分別執行：批量配置不同大小的區塊 → 釋放一半區塊製造空洞 → 印出
    `kmstat`（碎片化分析）→ 執行 2000 次配置/釋放循環並量測經過的 tick 數
    （延遲量測）→ 全部釋放後驗證 coalescing 是否能還原成單一空閒區塊。
- `kernel/main.c`、`Makefile` — 將 `kmallocinit()` 接到開機流程、`kmalloc.o` 加入
  核心物件、`_kmtest` 加入使用者程式清單。

## 系統呼叫介面

| 系統呼叫 | 簽名 | 說明 |
|---|---|---|
| `kmalloc` | `uint64 kmalloc(int size)` | 從核心 heap 配置 `size` bytes，回傳一個不透明的 handle（核心指標值），失敗回傳 0 |
| `kmfree` | `int kmfree(uint64 handle)` | 釋放 `kmalloc` 回傳的 handle |
| `kmpolicy` | `int kmpolicy(int policy)` | 切換配置策略：`KM_FIRST_FIT`=0、`KM_BEST_FIT`=1、`KM_WORST_FIT`=2 |
| `kmstat` | `int kmstat(struct kmstat *st)` | 取得目前配置器狀態快照 |

> handle 是核心位址值，使用者程式不會（也不應該）對其解參考，僅作為 `kmfree` 的識別碼傳回核心。

## 建置與執行（WSL Ubuntu）

```bash
cd ~/xv6-mem-project
make            # 編譯核心
make fs.img     # 編譯使用者程式並產生檔案系統影像
make qemu       # 在 QEMU 中開機（Ctrl-a x 離開）
```

開機後在 shell 中執行：

```
$ kmtest
```

即可看到三種策略各自的：
1. 初始狀態
2. 批量配置 64 個變動大小區塊後的狀態
3. 釋放一半區塊製造空洞後的碎片化統計（free_blk、largest_free）
4. 2000 次配置/釋放循環的耗時（tick 數）
5. 全部釋放後是否成功合併回單一空閒區塊（驗證 coalescing 正確性）

## 已完成 vs. 待擴充（對應提案章節）

- ✅ 二-1 核心級動態記憶體配置器：`kmalloc`/`kmfree`，不依賴 C 標準函式庫
- ✅ 二-2 多樣化連續配置策略：First-Fit / Best-Fit / Worst-Fit + 系統呼叫切換
- ✅ 二-3（前半）空洞合併：`kmfree` 中的前後鄰居 coalescing
- ⏳ 二-3（後半）記憶體緊縮（compaction）：尚未實作，屬提案中標註的挑戰項目，
  可在 `kmalloc.c` 中新增一個會搬移已配置區塊以重新整理 heap 的函式
- ✅ 三 開發環境：沿用 xv6 既有 QEMU + GDB 除錯流程
- ✅ 四 效能評估：`user/kmtest.c` 提供碎片化分析與延遲量測的雛形，可依需求增加
  測試規模、輸出格式或改用 `r_time()`（CSR）取得更精細的週期數
