#include <Windows.h>
#include <SDL.h>
extern "C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#include "resource.h"

typedef struct DecodeParam {
	AVFormatContext *formatContext;
	AVCodecContext  *codecContext;
	AVCodec *codec;
	AVFrame *frame;
	AVFrame *frameRGB;
	AVPacket *packet;
	SwsContext *swsContext;

	int videoIndex;
	int gotPicture;
	int delay;
	unsigned char *outBuffer;
}DecodeParam;

typedef struct YUVParam {
	int width;
	int height;
	int fps;
}YUVParam;

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

//打开文件对话框
int OpenDialog(HWND hwnd, TCHAR* fullPath, TCHAR* fileName);

//设置窗口中显示图像的具体区域
SDL_Rect SetShowRect(HWND hwnd, int imageWidth, int imageHeight);

//打开文件
int OpenFile(HWND hwnd, TCHAR* fullPath, TCHAR* fileName, DecodeParam *decodeParam, SDL_Rect *showRect);

//关闭文件
int CloseFile(DecodeParam *decodeParam);

INT_PTR CALLBACK YUVDialogProcedure(HWND, UINT, WPARAM, LPARAM);

INT_PTR CALLBACK AboutDialogProcedure(HWND, UINT, WPARAM, LPARAM);

int GetFileName(char *fileName, char *fullPath);

int GetFormat(char *format, char *fileName);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR szCmdLine, int iCmdShow) {
	av_register_all();

	SDL_Init(SDL_INIT_EVERYTHING);

	static TCHAR windowClassName[] = TEXT("PlayerWindow");
	HWND mainHwnd;
	MSG message;
	WNDCLASS windowClass;

	windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	windowClass.lpfnWndProc = WindowProcedure;
	windowClass.cbClsExtra = 0;
	windowClass.cbWndExtra = 0;
	windowClass.hInstance = hInstance;
	windowClass.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON));
	windowClass.hCursor = LoadCursor(0, IDC_ARROW);
	windowClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	windowClass.lpszMenuName = 0;
	windowClass.lpszClassName = windowClassName;

	if (!RegisterClass(&windowClass)) {
		MessageBox(0, TEXT("This program requires Windows NT !"), windowClassName, MB_ICONERROR);
		return 0;
	}

	mainHwnd = CreateWindow(windowClassName, TEXT("清风影音"), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, hInstance, 0);
	ShowWindow(mainHwnd, SW_MAXIMIZE);
	UpdateWindow(mainHwnd);

	while (GetMessage(&message, 0, 0, 0)) {
		TranslateMessage(&message);
		DispatchMessage(&message);
	}

	return message.wParam;
}

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	static HINSTANCE hInstance;

	static TCHAR fullPath[MAX_PATH];
	static TCHAR fileName[MAX_PATH];
	static TCHAR format[3];

	static int start = 0;	//0表示没有文件被打开，1表示有文件被打开
	static int play = 0;	//0表示处于暂停状态，1表示处于播放状态

	static DecodeParam decodeParam;

	static HMENU hmenu;
	static RECT windowRect;
	static RECT deskRect;
	static HWND deskHwnd;

	static int fullScreen = 0;
	static long windowStyle;

	static SDL_Window *sdlWindow = 0;
	static SDL_Renderer *sdlRenderer = 0;
	static SDL_Surface *sdlSurface = 0;
	static SDL_Texture *sdlTexture = 0;
	static SDL_Texture *sdlStartTexture = 0;
	static SDL_Rect sdlShowRect;	//窗口中显示图像的具体区域

	static FILE *yuvFile = 0;
	static unsigned char *yuvBuffer;
	static unsigned char *uBuffer;
	static unsigned char *vBuffer;
	static YUVParam yuvParam;

	switch (message) {
	case WM_CREATE:
		hInstance = ((LPCREATESTRUCT)lParam)->hInstance;
		hmenu = LoadMenu(((LPCREATESTRUCT)lParam)->hInstance, MAKEINTRESOURCE(IDR_MENU));
		SetMenu(hwnd, hmenu);
		sdlWindow = SDL_CreateWindowFrom(hwnd);
		sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
		sdlSurface = SDL_LoadBMP("player.bmp");
		sdlStartTexture = SDL_CreateTextureFromSurface(sdlRenderer, sdlSurface);
		SDL_FreeSurface(sdlSurface);

		if (__argc == 2) {
			strcpy(fullPath, __argv[1]);
			GetFileName(fileName, fullPath);
			GetFormat(format, fileName);
			if (strcmp(format, "yuv")==0 || strcmp(format, "YUV")==0) {
				PostMessage(hwnd, WM_COMMAND, IDM_FILE_OPENYUV, NULL);
			}
			else {
				PostMessage(hwnd, WM_COMMAND, IDM_FILE_OPEN, NULL);
			}
		}
		break;
	case WM_SIZE:
		if (start == 1) {
			sdlShowRect = SetShowRect(hwnd, decodeParam.codecContext->width, decodeParam.codecContext->height);
			SDL_RenderClear(sdlRenderer);
			SDL_RenderCopy(sdlRenderer, sdlTexture, 0, &sdlShowRect);
			SDL_RenderPresent(sdlRenderer);
		}
		else {
			SDL_RenderClear(sdlRenderer);
			SDL_RenderCopy(sdlRenderer, sdlStartTexture, 0, 0);
			SDL_RenderPresent(sdlRenderer);
		}
		break;
	case WM_COMMAND:
		switch (wParam) {
		case IDM_FILE_OPEN:
			if (start == 0) {
				if (__argc == 2) {
					__argc = 1;
				}
				else if(!OpenDialog(hwnd, fullPath, fileName)) {
					return -1;
				}
				if (!OpenFile(hwnd, fullPath, fileName, &decodeParam, &sdlShowRect)) {
					return -1;
				}
				sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, decodeParam.codecContext->width, decodeParam.codecContext->height);
				SetTimer(hwnd, 0, decodeParam.delay, 0);
				play = 1;
				start = 1;
			}
			else {
				if (play == 1) {
					KillTimer(hwnd, 0);
					play = 0;
				}
				CloseFile(&decodeParam);
				SDL_DestroyTexture(sdlTexture);
				if (!OpenDialog(hwnd, fullPath, fileName)) {
					return -1;
				}
				if (!OpenFile(hwnd, fullPath, fileName, &decodeParam, &sdlShowRect)) {
					return -1;
				}
				sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, decodeParam.codecContext->width, decodeParam.codecContext->height);
				SetTimer(hwnd, 0,decodeParam.delay, 0);
				play = 1;
			}
			break;
		case IDM_FILE_OPENYUV:
			if (__argc == 2) {
				__argc = 1;
			}
			else if (!OpenDialog(hwnd, fullPath, fileName)) {
				return -1;
			}
			DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_PROPPAGE_MEDIUM), hwnd, YUVDialogProcedure, (LPARAM)&yuvParam);
			yuvFile = fopen(fullPath,"rb");
			sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, yuvParam.width, yuvParam.height);
			sdlShowRect = SetShowRect(hwnd, yuvParam.width, yuvParam.height);
			yuvBuffer = new unsigned char[yuvParam.width*yuvParam.height * 3 / 2];
			uBuffer = yuvBuffer + yuvParam.width*yuvParam.height;
			vBuffer = uBuffer + yuvParam.width*yuvParam.height / 4;
			SetTimer(hwnd, 1, 1000 / yuvParam.fps, 0);
			break;
		case IDM_FILE_CLOSE:
			if (play == 1) {
				KillTimer(hwnd, 0);
			}
			if (start == 1) {
				CloseFile(&decodeParam);
				SDL_DestroyTexture(sdlTexture);
			}

			SDL_DestroyTexture(sdlStartTexture);
			SDL_DestroyRenderer(sdlRenderer);
			SDL_DestroyWindow(sdlWindow);

			DestroyWindow(hwnd);
			break;
		case IDM_TOOL_CAPTURE:
			break;
		case IDM_HELP_ABOUT:
			DialogBox(hInstance, MAKEINTRESOURCE(IDD_PROPPAGE_LARGE), hwnd, AboutDialogProcedure);
			break;
		}
		break;
	case WM_LBUTTONDBLCLK:
		if (!fullScreen) {
			windowStyle = GetWindowLong(hwnd, GWL_STYLE);
			GetWindowRect(hwnd, &windowRect);
			deskHwnd = GetDesktopWindow();
			GetWindowRect(deskHwnd, &deskRect);
			SetWindowLong(hwnd, GWL_STYLE, WS_CHILD);
			SetWindowPos(hwnd, HWND_TOP, 0, 0, deskRect.right, deskRect.bottom, SWP_SHOWWINDOW);
			fullScreen = 1;
		}
		else {
			SetWindowLong(hwnd, GWL_STYLE, windowStyle);
			MoveWindow(hwnd, windowRect.left, windowRect.top, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, true);
			SetMenu(hwnd, hmenu);
			fullScreen = 0;
		}

		if (start == 1) {
			if (play == 0) {
				SetTimer(hwnd, 0,decodeParam.delay, 0);
				play = 1;
			}
			else {
				KillTimer(hwnd, 0);
				play = 0;
			}
		}
		break;
	case WM_LBUTTONDOWN:
		if (start == 1) {
			if (play == 0) {
				SetTimer(hwnd, 0,decodeParam.delay, 0);
				play = 1;
			}
			else {
				KillTimer(hwnd, 0);
				play = 0;
			}
		}
		break;
	case WM_KEYUP:
		if (wParam == VK_SPACE) {
			if (start == 1) {
				if (play == 0) {
					SetTimer(hwnd, 0, decodeParam.delay, 0);
					play = 1;
				}
				else {
					KillTimer(hwnd, 0);
					play = 0;
				}
			}
		}
		break;
	case WM_TIMER:
		if (wParam == 0) {
			while (1) {
				if (av_read_frame(decodeParam.formatContext, decodeParam.packet) < 0) {
					KillTimer(hwnd, 0);
					CloseFile(&decodeParam);
					SDL_DestroyTexture(sdlTexture);
					MessageBox(0, TEXT("播放结束！"), TEXT("视频消息"), MB_OK);
					start = 0;
					break;
				}
				if (decodeParam.packet->stream_index == decodeParam.videoIndex) {
					if (avcodec_decode_video2(decodeParam.codecContext, decodeParam.frame, &decodeParam.gotPicture, decodeParam.packet) < 0) {
						MessageBox(0, TEXT("解码错误！"), 0, MB_ICONERROR);
						return -1;
					}
					if (decodeParam.gotPicture) {
						sws_scale(decodeParam.swsContext, (const uint8_t* const*)decodeParam.frame->data, decodeParam.frame->linesize, 0, decodeParam.codecContext->height, decodeParam.frameRGB->data, decodeParam.frameRGB->linesize);
						SDL_UpdateTexture(sdlTexture, 0, decodeParam.frameRGB->data[0], decodeParam.frameRGB->linesize[0]);
						SDL_RenderClear(sdlRenderer);
						SDL_RenderCopy(sdlRenderer, sdlTexture, 0, &sdlShowRect);
						SDL_RenderPresent(sdlRenderer);
						av_free_packet(decodeParam.packet);
						break;
					}
				}
				av_free_packet(decodeParam.packet);
			}
		}
		else if (wParam == 1) {
			if (!fread(yuvBuffer, 1, yuvParam.width*yuvParam.height * 3 / 2, yuvFile)) {
				KillTimer(hwnd, 1);
				fclose(yuvFile);
			}
			SDL_UpdateYUVTexture(sdlTexture, 0, yuvBuffer, yuvParam.width, uBuffer, yuvParam.width / 2, vBuffer, yuvParam.width / 2);
			SDL_RenderClear(sdlRenderer);
			SDL_RenderCopy(sdlRenderer, sdlTexture, 0, &sdlShowRect);
			SDL_RenderPresent(sdlRenderer);
		}

		break;
	case WM_CLOSE:
		if (play == 1) {
			KillTimer(hwnd, 0);
		}
		if (start == 1) {
			CloseFile(&decodeParam);
			SDL_DestroyTexture(sdlTexture);
		}
		SDL_DestroyTexture(sdlStartTexture);
		SDL_DestroyRenderer(sdlRenderer);
		SDL_DestroyWindow(sdlWindow);
		DestroyWindow(hwnd);
		break;
	case WM_DESTROY:
		SDL_Quit();
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hwnd, message, wParam, lParam);
	}
	return 0;
}

int OpenDialog(HWND hwnd, TCHAR* fullPath, TCHAR* fileName) {
	static OPENFILENAME openFileName;
	static TCHAR fileFormat[] = TEXT("视频文件(*.mp4;*.avi;*.rmvb;*mkv;*yuv;*wmv)\0*.mp4;*.rmvb;*.avi;*.mkv;*.yuv;*.wmv\0所有文件(*.*)\0*.*\0\0");
	openFileName.lStructSize = sizeof(OPENFILENAME);
	openFileName.hwndOwner = hwnd;
	openFileName.hInstance = 0;
	openFileName.lpstrFilter = fileFormat;
	openFileName.lpstrCustomFilter = 0;
	openFileName.nMaxCustFilter = 0;
	openFileName.nFilterIndex = 0;
	openFileName.lpstrFile = fullPath;
	openFileName.nMaxFile = MAX_PATH;
	openFileName.lpstrFileTitle = fileName;
	openFileName.nMaxFileTitle = MAX_PATH;
	openFileName.lpstrInitialDir = 0;
	openFileName.lpstrTitle = 0;
	openFileName.Flags = OFN_HIDEREADONLY;
	openFileName.nFileOffset = 0;
	openFileName.nFileExtension = 0;
	openFileName.lpstrDefExt = TEXT("mp4");
	openFileName.lCustData = 0;
	openFileName.lpfnHook = 0;
	openFileName.lpTemplateName = 0;
	return GetOpenFileName(&openFileName);
}

SDL_Rect SetShowRect(HWND hwnd, int imageWidth, int imageHeight) {
	SDL_Rect showRect;
	RECT dstRect;

	double dstW;	//显示区宽
	double dstH;	//显示区高
	double srcWH;	//图像宽高比
	double dstWH;	//显示区宽高比
	double dstSrcW;	//显示区宽/图像宽
	double dstSrcH;	//显示区高/图像高

	GetClientRect(hwnd, &dstRect);
	dstW = dstRect.right - dstRect.left;
	dstH = dstRect.bottom - dstRect.top;
	srcWH = (double)imageWidth / (double)imageHeight;
	dstWH = dstW / dstH;
	dstSrcW = dstW / imageWidth;
	dstSrcH = dstH / imageHeight;
	if (srcWH > dstWH) {
		showRect.x = 0;
		showRect.y = dstH / 2 - dstSrcW*imageHeight / 2;
		showRect.w = dstW;
		showRect.h = dstSrcW*imageHeight;
	}
	else if (srcWH < dstWH) {
		showRect.x = dstW / 2 - dstSrcH*imageWidth / 2;
		showRect.y = 0;
		showRect.w = dstSrcH*imageWidth;
		showRect.h = dstH;
	}
	else {
		showRect.x = 0;
		showRect.y = 0;
		showRect.w = dstW;
		showRect.h = dstH;
	}
	return showRect;
}

int OpenFile(HWND hwnd, TCHAR* fullPath, TCHAR* fileName, DecodeParam *decodeParam, SDL_Rect *showRect) {
	int width;
	int height;
	SDL_Rect rect;

	SetWindowText(hwnd, fileName);

	decodeParam->formatContext = avformat_alloc_context();
	if (avformat_open_input(&decodeParam->formatContext, fullPath, 0, 0) != 0) {
		MessageBox(0, TEXT("无法打开文件！"), 0, MB_ICONERROR);
		return 0;
	}
	if (avformat_find_stream_info(decodeParam->formatContext, 0)<0) {
		MessageBox(0, TEXT("无法找到流信息！"), 0, MB_ICONERROR);
		return 0;
	}
	decodeParam->videoIndex = -1;
	for (unsigned int i = 0; i<decodeParam->formatContext->nb_streams; i++)
		if (decodeParam->formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			decodeParam->videoIndex = i;
			break;
		}
	if (decodeParam->videoIndex == -1) {
		MessageBox(0, TEXT("未发现视频流！"), 0, MB_ICONERROR);
		return 0;
	}
	decodeParam->delay = 1000.0 / decodeParam->formatContext->streams[decodeParam->videoIndex]->avg_frame_rate.num * decodeParam->formatContext->streams[decodeParam->videoIndex]->avg_frame_rate.den;
	decodeParam->codecContext = decodeParam->formatContext->streams[decodeParam->videoIndex]->codec;
	decodeParam->codec = avcodec_find_decoder(decodeParam->codecContext->codec_id);
	if (decodeParam->codec == 0) {
		MessageBox(0, TEXT("未找到解码器！"), 0, MB_ICONERROR);
		return 0;
	}
	if (avcodec_open2(decodeParam->codecContext, decodeParam->codec, 0)<0) {
		MessageBox(0, TEXT("无法打开解码器！"), 0, MB_ICONERROR);
		return 0;
	}
	width = decodeParam->codecContext->width;
	height = decodeParam->codecContext->height;
	decodeParam->frame = av_frame_alloc();
	decodeParam->frameRGB = av_frame_alloc();
	decodeParam->outBuffer = (uint8_t *)av_malloc(avpicture_get_size(AV_PIX_FMT_RGB24, width, height));
	avpicture_fill((AVPicture *)decodeParam->frameRGB, decodeParam->outBuffer, AV_PIX_FMT_RGB24, width, height);
	decodeParam->packet = (AVPacket *)av_malloc(sizeof(AVPacket));
	decodeParam->swsContext = sws_getContext(width, height, decodeParam->codecContext->pix_fmt, width, height, AV_PIX_FMT_RGB24, SWS_BICUBIC, 0, 0, 0);
	rect = SetShowRect(hwnd, width, height);
	showRect->x = rect.x;
	showRect->y = rect.y;
	showRect->w = rect.w;
	showRect->h = rect.h;
	return 1;
}

int CloseFile(DecodeParam *decodeParam) {
	sws_freeContext(decodeParam->swsContext);
	av_frame_free(&decodeParam->frameRGB);
	av_frame_free(&decodeParam->frame);
	avcodec_close(decodeParam->codecContext);
	avformat_close_input(&decodeParam->formatContext);
	return 1;
}

INT_PTR CALLBACK DialogProcedure(HWND hwndDlg, UINT UMsg, WPARAM wParam, LPARAM lParam) {
	switch (UMsg)
	{
	case WM_INITDIALOG:
		break;
	case WM_CLOSE:
		DestroyWindow(hwndDlg);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		break;
	}
	return 0;
}

INT_PTR CALLBACK YUVDialogProcedure(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	static YUVParam *yuv;
	switch (message) {
	case WM_INITDIALOG:
		yuv = (YUVParam*)lParam;
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_BUTTON1:
			yuv->width = GetDlgItemInt(hDlg, IDC_EDIT1, 0, 0);
			yuv->height = GetDlgItemInt(hDlg, IDC_EDIT2, 0, 0);
			yuv->fps = GetDlgItemInt(hDlg, IDC_EDIT3, 0, 0);
			EndDialog(hDlg, 0);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

INT_PTR CALLBACK AboutDialogProcedure(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam){
	switch (message){
	case WM_INITDIALOG:
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_BUTTON1:
			EndDialog(hDlg, 0);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

int GetFileName(char *fileName,char *fullPath) {
	char *c = fullPath;
	while (c = strstr(c, "\\")) {
		c++;
		strcpy(fileName, c);
	}
	return 1;
}

int GetFormat(char *format, char *fileName) {
	int i = strlen(fileName);
	format[0] = fileName[i - 3];
	format[1] = fileName[i - 2];
	format[2] = fileName[i - 1];
	return 1;
}