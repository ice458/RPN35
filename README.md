# RPN35
RP2350Aを用いた RPN関数電卓のファームウェアです。

## ビルド
- Raspberry Pi Pico VS Code Extensionの使用を推奨します。

## 構成
- ルート直下の `*.c`/`*.h`: 本体ソース
- `build/`: ビルド生成物(.gitignoreで無視)
- `LICENCE`: プロジェクトライセンス（BSD 3-Clause）
- `licenses/vendor-intel-dfp-eula.txt`: Intel Decimal Floating-Point Math Library のライセンス文
- `THIRD_PARTY_LICENSES.md`: サードパーティ告知（改変ライブラリのクレジット等）

## ライセンス
- このリポジトリのプロジェクトコードは、BSD 3-Clause License の下で提供されます。詳細は `LICENCE` を参照してください。
- サードパーティのライセンスについては、`THIRD_PARTY_NOTICES.md` に記載されています。
- IntelRDFPMathLibと共に配布されたIntelのEULAの逐語的なコピーが、便宜上 `licenses/vendor-intel-dfp-eula.txt` に含まれています。注意: 元の `IntelRDFPMathLib20U2/eula.txt` は `.gitignore` によって無視されるため、コミットされていません。

### サードパーティコンポーネント
- **Intel Decimal Floating-Point Math Library**: BSD 3-Clause（`licenses/vendor-intel-dfp-eula.txt` を参照）。
- **改変ヘッダ (`bid_conf.h`, `bid_functions.h`)**: Intelのヘッダから派生し、Pico互換性のための調整が加えられていますが、元のライセンスヘッダを保持しています。

## 依存関係
- Raspberry Pi Pico SDK (BSD 3-Clause License)
- Intel Decimal Floating-Point Math Library (BSD 3-Clause License)  
    - gcc111libbid_pico2.aとして組み込み済み
