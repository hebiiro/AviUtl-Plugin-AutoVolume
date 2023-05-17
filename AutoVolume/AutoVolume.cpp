#include "pch.h"

//--------------------------------------------------------------------

// デバッグ用コールバック関数。デバッグメッセージを出力する。
void ___outputLog(LPCTSTR text, LPCTSTR output)
{
	::OutputDebugString(output);
}

//--------------------------------------------------------------------

struct Track
{
	static const int32_t VolumeL = 0;
	static const int32_t VolumeR = 1;
	static const int32_t ExcludeLayer = 2;
	static const int32_t TargetLayer = 3;
	static const int32_t Fadein = 4;
	static const int32_t Fadeout = 5;
	static const int32_t BeginBlank = 6;
	static const int32_t EndBlank = 7;
};

struct Check
{
	static const int32_t Enable = 0;
};

//--------------------------------------------------------------------

AviUtlInternal g_auin;

//--------------------------------------------------------------------

BOOL func_init(AviUtl::FilterPlugin* fp)
{
	if (!g_auin.initExEditAddress())
		return FALSE;

	return TRUE;
}

BOOL func_exit(AviUtl::FilterPlugin* fp)
{
	return FALSE;
}

BOOL existsUnderAudio(AviUtl::FilterPlugin* fp, AviUtl::FilterProcInfo* fpip, ExEdit::Object* _object, double* strength)
{
	MY_TRACE(_T("existsUnderAudio()\n"));

	// _object より下にあり、現在フレームと交差する音声アイテムを検索する。
	// 監視対象の音声アイテムは 1 つとは限らないので、必ず全数チェックする必要がある。
	// ↑監視対象の音声アイテムの数が監視対象レイヤー数と同じになったらループを終了してもいいかもしれない。

	int layer_set = _object->layer_set + fp->track[Track::ExcludeLayer];
	MY_TRACE_INT(layer_set);

	// 例えばプロジェクトが 60fps の場合は
	// fpip->editp->video_rate == 0, fpip->editp->video_scale == 60
	// のようになるので、0 かどうかチェックしなければならない。
	int video_rate = (fpip->editp->video_rate == 0) ? 1 : fpip->editp->video_rate;
	int video_scale = (fpip->editp->video_scale == 0) ? 60 : fpip->editp->video_scale;
	double ms2frame =  1.0 / 1000.0 * video_scale / video_rate; // ミリ秒をフレーム単位に変換する倍率。

	// 各設定値をフレーム単位に変換する。
	int fadein = (int)(fp->track[Track::Fadein] * ms2frame);
	int fadeout = (int)(fp->track[Track::Fadeout] * ms2frame);
	int beginBlank = (int)(fp->track[Track::BeginBlank] * ms2frame);
	int endBlank = (int)(fp->track[Track::EndBlank] * ms2frame);

	// 音声アイテムが見つかったときはこれに TRUE を入れる。
	BOOL retValue = FALSE;

	// 全てのオブジェクトの数を取得する。
	int c = g_auin.GetObjectCount();
	MY_TRACE_INT(c);

	// 全てのオブジェクトをチェックする。
	// i は c 回ループするためのカウンタ。index はオブジェクトのインデックス。
	for (int i = 0, index = 0; i < c; i++, index++)
	{
		// オブジェクトを取得する。
		ExEdit::Object* object = g_auin.GetObject(index);

		{
			// 無効な object をスキップする。

			while (1)
			{
				// Exist フラグがないオブジェクトはスキップする。
				if ((uint32_t)object->flag & (uint32_t)ExEdit::Object::Flag::Exist)
					break;

				object = g_auin.GetObject(++index);
			}
		}

		// ここからは基本的に object が条件に合わなかったら continue する。

		MY_TRACE_HEX(object->flag);

		if (!((uint32_t)object->flag & (uint32_t)ExEdit::Object::Flag::Sound))
		{
			MY_TRACE(_T("音声オブジェクトではなかった\n"));
			continue;
		}

		if (!((uint32_t)object->flag & (uint32_t)ExEdit::Object::Flag::Media))
		{
			MY_TRACE(_T("メディアオブジェクトではなかった\n"));
			continue;
		}

		if (object->scene_set != _object->scene_set)
		{
			MY_TRACE(_T("別のシーンのオブジェクトだった\n"));
			continue;
		}

		if (object->layer_set <= layer_set)
		{
			MY_TRACE(_T("自分より上のレイヤーにあるオブジェクトだった\n"));
			continue;
		}

		if (fp->track[Track::TargetLayer] == 0)
		{
			// 自分より下の全てのレイヤーが対象。
		}
		else if (fp->track[Track::TargetLayer] > 0)
		{
			// 自分より下の指定された個数のレイヤーが対象。

			if (object->layer_set - layer_set > fp->track[Track::TargetLayer])
			{
				MY_TRACE(_T("指定された個数以上のレイヤーだった\n"));
				continue;
			}
		}
#if 0
		else // fp->track[Track::TargetLayer] が負数の場合
		{
			// 自分より下の指定された個数を除くレイヤーが対象。

			if (object->layer_set - layer_set <= -fp->track[Track::TargetLayer])
			{
				MY_TRACE(_T("指定された個数以下のレイヤーだった\n"));
				continue;
			}
		}
#endif
		// オブジェクトがあるレイヤーの設定を取得する。
		ExEdit::LayerSetting* layer = g_auin.GetLayerSetting(object->layer_set);

		if ((uint32_t)layer->flag & (uint32_t)ExEdit::LayerSetting::Flag::UnDisp)
		{
			MY_TRACE(_T("オブジェクトがあるレイヤーが非表示だった\n"));
			continue;
		}

		// ここからは条件によってはマッチする。

		if (fpip->frame < object->frame_begin)
		{
			MY_TRACE(_T("現在フレームがオブジェクトの開始位置より前だった\n"));

			if (beginBlank != 0 && fpip->frame >= object->frame_begin - beginBlank)
			{
				MY_TRACE(_T("現在フレームが開始ブランク内だった\n"));
				retValue = TRUE;
				*strength = 1.0;
			}
			else if (fadein != 0 && fpip->frame >= object->frame_begin - beginBlank - fadein)
			{
				MY_TRACE(_T("現在フレームがフェードイン内だった\n"));
				retValue = TRUE;
				double temp = (double)(fpip->frame - (object->frame_begin - beginBlank - fadein)) / fadein;
				MY_TRACE_REAL(temp);
				*strength = std::max(*strength, temp);
				MY_TRACE_REAL(*strength);
			}

			continue;
		}

		if (fpip->frame > object->frame_end)
		{
			MY_TRACE(_T("現在フレームがオブジェクトの終了位置より前だった\n"));

			if (endBlank != 0 && fpip->frame <= object->frame_end + endBlank)
			{
				MY_TRACE(_T("現在フレームが終了ブランク内だった\n"));
				retValue = TRUE;
				*strength = 1.0;
			}
			else if (fadeout != 0 && fpip->frame <= object->frame_end + endBlank + fadeout)
			{
				MY_TRACE(_T("現在フレームがフェードアウト内だった\n"));
				retValue = TRUE;
				double temp = (double)((object->frame_end + endBlank + fadeout) - fpip->frame) / fadeout;
				MY_TRACE_REAL(temp);
				*strength = std::max(*strength, temp);
				MY_TRACE_REAL(*strength);
			}

			continue;
		}

		MY_TRACE(_T("現在フレームとオブジェクトが交差していた\n"));

		retValue = TRUE;
		*strength = 1.0;
	}

	return retValue;
}

BOOL adjustVolume(AviUtl::FilterPlugin* fp, AviUtl::FilterProcInfo* fpip, ExEdit::Object* _object, double strength)
{
	MY_TRACE(_T("adjustVolume()\n"));

	// strength は 0.0 ~ 1.0 を想定。
	// strength が 0.0 の場合は音量を 100% にする。よって scale を 1.0 にする。
	// strength が 1.0 の場合は音量をユーザーが指定した倍率になるように変更する。

	double scale[2] =
	{
		std::lerp(1.0, fp->track[Track::VolumeL] / 100.0, strength),
		std::lerp(1.0, fp->track[Track::VolumeR] / 100.0, strength),
	};

	// チェンネル数でループ。
	int audio_ch = std::min(fpip->audio_ch, 2);
	for (int j = 0; j < audio_ch; j++)
	{
		// 100% が指定されている場合は何もしない。
		if (fp->track[Track::VolumeL + j] == 100)
			continue;

		// サンプル数でループ。
		int audio_n = fpip->audio_n;
		for (int i = 0; i < audio_n; i++)
		{
			int index = fpip->audio_ch * i + j;

			fpip->audiop[index] = (short)(fpip->audiop[index] * scale[j]);
		}
	}

	return TRUE;
}

BOOL func_proc_internal(AviUtl::FilterPlugin* fp, AviUtl::FilterProcInfo* fpip, ExEdit::Object* _object)
{
	ExEdit::Object object = {};
	if (!::ReadProcessMemory(::GetCurrentProcess(), _object, &object, sizeof(object), 0))
	{
		MY_TRACE(_T("メモリを読み込めませんでした\n"));

		return FALSE;
	}

	if ((uint32_t)object.flag & (uint32_t)ExEdit::Object::Flag::Exist)
	{
		int layerIndex = object.layer_set;
		if (layerIndex < 0 || layerIndex >= 100)
		{
			MY_TRACE(_T("レイヤーインデックスが不正です\n"));

			return FALSE;
		}

		MY_TRACE_INT(layerIndex);

		// layerIndex より下に音声アイテムがあるか調べる。
		double strength = 0.0;
		if (existsUnderAudio(fp, fpip, &object, &strength))
		{
			// 現在の音量を調整する。
			adjustVolume(fp, fpip, &object, strength);
		}
	}
	else
	{
		MY_TRACE(_T("オブジェクトが不正です\n"));

		return FALSE;
	}

	return TRUE;
}

__declspec(naked) BOOL func_proc(AviUtl::FilterPlugin* fp, AviUtl::FilterProcInfo* fpip)
{
	// fpip を ExEdit::FilterProcInfo* にキャストしても
	// objectp などが 0 になっていてオブジェクトを取得できない。
	// よって、EDI に入っているオブジェクトを取得して func_proc_internal() に渡す。

	__asm
	{
		MOV EDX, [ESP+0x08] // fpip
		MOV ECX, [ESP+0x04] // fp
		PUSH EDI // object
		PUSH EDX // fpip
		PUSH ECX // fp
		CALL func_proc_internal // func_proc_internal() の呼び出し。
		ADD ESP, 0x0C // 3 引数の後始末。
		RET // リターン。
	}
}

//--------------------------------------------------------------------

LPCSTR track_name[] =
{
	"音量L",
	"音量R",
	"除外ﾚｲﾔｰ数",
	"対象ﾚｲﾔｰ数",
	"ﾌｪｰﾄﾞｲﾝ",
	"ﾌｪｰﾄﾞｱｳﾄ",
	"開始ﾌﾞﾗﾝｸ",
	"終了ﾌﾞﾗﾝｸ",
};

int track_def[] = { 100, 100,   0,   0,     0,     0,     0,     0 };
int track_min[] = {   0,   0,   0,   0,     0,     0,     0,     0 };
int track_max[] = { 100, 100, 100, 100, 10000, 10000, 10000, 10000 };

LPCSTR check_name[] =
{
	"テスト用",
};

int check_def[] =
{
	TRUE,
};

EXTERN_C AviUtl::FilterPluginDLL* WINAPI GetFilterTable()
{
	LPCSTR name = "オートボリューム";
	LPCSTR information = "オートボリューム 1.0.0 by 蛇色";

	static AviUtl::FilterPluginDLL filter =
	{
		.flag =
//			AviUtl::FilterPlugin::Flag::AlwaysActive |
			AviUtl::FilterPlugin::Flag::AudioFilter |
			AviUtl::FilterPlugin::Flag::ExInformation,
		.name = name,
		.track_n = sizeof(track_name) / sizeof(*track_name),
		.track_name = track_name,
		.track_default = track_def,
		.track_s = track_min,
		.track_e = track_max,
		.check_n = sizeof(check_name) / sizeof(*check_name),
		.check_name = check_name,
		.check_default = check_def,
		.func_proc = func_proc,
		.func_init = func_init,
		.func_exit = func_exit,
		.information = information,
	};

	return &filter;
}

//--------------------------------------------------------------------
