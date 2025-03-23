// pch.h: プリコンパイル済みヘッダー ファイルです。
// 次のファイルは、その後のビルドのビルド パフォーマンスを向上させるため 1 回だけコンパイルされます。
// コード補完や多くのコード参照機能などの IntelliSense パフォーマンスにも影響します。
// ただし、ここに一覧表示されているファイルは、ビルド間でいずれかが更新されると、すべてが再コンパイルされます。
// 頻繁に更新するファイルをここに追加しないでください。追加すると、パフォーマンス上の利点がなくなります。

#ifndef PCH_H
#define PCH_H

// プリコンパイルするヘッダーをここに追加します
#include "framework.h"


// ngl
#include "math/math.h"
#include "text/hash_text.h"
#include "boot/boot_application.h"
#include "file/file.h"
#include "platform/window.h"
#include "thread/job_thread.h"
#include "resource/resource.h"

#include "rhi/rhi.h"
#include "rhi/rhi_ref.h"

// external
#include "tinyxml2.h"



#endif //PCH_H
