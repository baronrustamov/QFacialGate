#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/sockios.h>
#include <arpa/inet.h>
#include <QtWidgets>
#include <QPainter>
#include <QPaintEngine>
#include <QDateTime>
#include "videoitem.h"

VideoItem::VideoItem(const QRect &rect, QGraphicsItem *parent)
	: QGraphicsObject(parent)
{
#ifdef QT_FB_DRM_RGB565
	rgaFormat = RK_FORMAT_RGB_565;
#else
	rgaFormat = RK_FORMAT_BGRA_8888;
#endif

	//blend = 0xFF0105;
	blend = 0xFF0405;
	displayRect = rect;
	infoBox.infoRect.setRect(displayRect.x(), displayRect.height()*4/5, displayRect.width(), displayRect.height()/5);
	infoBox.titleRect = infoBox.infoRect.adjusted(10, 10, 0, 0);
	infoBox.ipRect = infoBox.titleRect.adjusted(40, 70, 0, 0);
	infoBox.timeRect = infoBox.ipRect.adjusted(0, 50, 0, 0);
	infoBox.nameRect = infoBox.timeRect.adjusted(0, 70, 0, 0);
	infoBox.snapshotRect = QRectF(550, 1070, 150, 150);
	infoBox.title = tr("人脸识别");

	memset(&facial, 0, sizeof(struct FacialInfo));
	memset(&video, 0, sizeof(struct VideoInfo));

	int len = infoBox.infoRect.width() * infoBox.infoRect.height();
	int r = 153, g = 51, b = 250, a = 100;
	int bgra = ((a & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
	infoBoxBuf = (int *)malloc(len * sizeof(int));
	if(infoBoxBuf) {
		for(int i = 0; i < len; i++)
			infoBoxBuf[i] = bgra;
	}

	defaultSnapshot.load(":/images/default_user_face.png");
	snapshotThread = new SnapshotThread();

	initTimer();
}

VideoItem::~VideoItem()
{
	c_RkRgaDeInit();
	timer->stop();
	if(infoBoxBuf) {
		free(infoBoxBuf);
		infoBoxBuf = NULL;
	}
}

static int getLocalIp(char *interface, char *ip, int ip_len)
{
	int sd;
	struct sockaddr_in sin;
	struct ifreq ifr;

	memset(ip, 0, ip_len);
	sd = socket(AF_INET, SOCK_DGRAM, 0);
	if (-1 == sd) {
		//qDebug("socket error: %s\n", strerror(errno));
		return -1;
	}

	strncpy(ifr.ifr_name, interface, IFNAMSIZ);
	ifr.ifr_name[IFNAMSIZ - 1] = 0;

	if (ioctl(sd, SIOCGIFADDR, &ifr) < 0) {
		//qDebug("ioctl error: %s\n", strerror(errno));
		close(sd);
		return -1;
	}

	memcpy(&sin, &ifr.ifr_addr, sizeof(sin));
	sprintf(ip, "%s", inet_ntoa(sin.sin_addr));

	close(sd);
	return 0;
}

void VideoItem::initTimer()
{
	timer = new QTimer;
	timer->setSingleShot(false);
	timer->start(3000); //ms
	connect(timer, SIGNAL(timeout()), this, SLOT(timerTimeOut()));
}

void VideoItem::timerTimeOut()
{
#if 0
	static QTime t_time;
	static int cnt = 1;

	if(cnt) {
		t_time.start();
		cnt--;
	}

	qDebug("%s: %ds", __func__, t_time.elapsed() / 1000);
#endif

	mutex.lock();
	getLocalIp("eth0", ip, 20);
	mutex.unlock();
}

void VideoItem::render(uchar *buf, RgaSURF_FORMAT format, int rotate, int width, int height)
{

	mutex.lock();

#if 0
	//printf fps
	static QTime cb_time;
	static int cb_frames = 0;

	if (!cb_frames)
		cb_time.start();

	if(cb_time.elapsed()/1000 >= 10) {
		cb_frames = 0;
		cb_time.restart();
	}

	if(!(++cb_frames % 50))
		printf("+++++ %s FPS: %2.2f (%u frames in %ds)\n",
				__func__, cb_frames * 1000.0 / cb_time.elapsed(),
				cb_frames, cb_time.elapsed() / 1000);
#endif

	video.buf = buf;
	video.format = format;
	video.rotate = rotate;
	video.width = width;
	video.height = height;

	mutex.unlock();
}

QRectF VideoItem::boundingRect() const
{
	return QRectF(displayRect.x(), displayRect.y(), displayRect.width(), displayRect.height());
}

void VideoItem::updateSlots()
{
#if 0 
		//printf fps
		static QTime u_time;
		static int u_frames = 0;
	
		if (!u_frames)
			u_time.start();
	
		if(!(++u_frames % 50))
			printf("***** %s FPS: %2.2f (%u frames in %ds)\n",
					__func__, u_frames * 1000.0 / u_time.elapsed(),
					u_frames, u_time.elapsed() / 1000);
#endif

	update();
}

void VideoItem::setBoxRect(int left, int top, int right, int bottom)
{
	mutex.lock();

	if(abs(facial.boxRect.left() - left) > MIN_POS_DIFF || abs(facial.boxRect.top() - top) > MIN_POS_DIFF
		|| abs(facial.boxRect.right() - right) > MIN_POS_DIFF || abs(facial.boxRect.bottom() - bottom) > MIN_POS_DIFF) {
		facial.boxRect.setCoords(left, top, right, bottom);
	}

	mutex.unlock();
}

void VideoItem::setName(char *name, bool real)
{
	int len;

	mutex.lock();

	if(name) {
		len = strlen(name) > (NAME_LEN - 1) ? (NAME_LEN - 1) : strlen(name);
		if(strncmp(facial.fullName, name, len)) {
			memset(facial.fullName, 0, NAME_LEN);
			strncpy(facial.fullName, name, len);
		}
	} else {
		if(strlen(facial.fullName))
			memset(facial.fullName, 0, sizeof(NAME_LEN));
	}

	facial.real = real;
	if(real) {
		if(snapshotThread->setName(name))
			snapshotThread->start();
	}

	mutex.unlock();
}

static bool findName(char *fullName, char *name, int nameLen)
{
	bool blackList = false;
	char *end, *begin;

	memset(name, 0, nameLen);
	if(strlen(fullName)) {
		begin = strrchr(fullName, '/');
		end = strrchr(fullName, '.');

		if (begin && end && end > begin)
			memcpy(name, begin + 1, end - begin - 1);
		else
			strcpy(name, "unknown_user");

		if (strstr(fullName, "black_list"))
			blackList = true;
	} 

	return blackList;
}

static int rgaPrepareInfo(uchar *buf, RgaSURF_FORMAT format, QRectF rect,
				int sw, int sh, rga_info_t *info)
{
	memset(info, 0, sizeof(rga_info_t));

	info->fd = -1;
	info->virAddr = buf;
	info->mmuFlag = 1;

	return rga_set_rect(&info->rect, rect.x(), rect.y(), rect.width(), rect.height(), sw, sh, format);
}

static int rgaDrawImage(uchar *src, RgaSURF_FORMAT src_format, QRectF srcRect,
				int src_sw, int src_sh, uchar *dst, RgaSURF_FORMAT dst_format,
				QRectF dstRect, int dst_sw, int dst_sh, int rotate, unsigned int blend)
{
	static int rgaSupported = 1;
	static int rgaInited = 0;
	rga_info_t srcInfo;
	rga_info_t dstInfo;

	memset(&srcInfo, 0, sizeof(rga_info_t));
	memset(&dstInfo, 0, sizeof(rga_info_t));

	if (!rgaSupported)
		return -1;

	if (!rgaInited) {
		if (c_RkRgaInit() < 0) {
			rgaSupported = 0;
			return -1;
		}

		rgaInited = 1;
	}

	if (rgaPrepareInfo(src, src_format, srcRect, src_sw, src_sh, &srcInfo) < 0)
		return -1;

	if (rgaPrepareInfo(dst, dst_format, dstRect, dst_sw, dst_sh, &dstInfo) < 0)
		return -1;

	srcInfo.rotation = rotate;
	if(blend)
		srcInfo.blend = blend;

	return c_RkRgaBlit(&srcInfo, &dstInfo, NULL);
}

void VideoItem::drawSnapshot(QPainter *painter, QImage *image)
{
	if(!facial.real) {
		painter->drawImage(infoBox.snapshotRect, defaultSnapshot);
		return;
	}

	if(!snapshotThread->snapshotBuf() && strlen(facial.fullName)) {
		if(!snapshotThread->isRunning())
			snapshotThread->start();

		painter->drawImage(infoBox.snapshotRect, defaultSnapshot);
		return;
	}

	float scale = snapshotThread->snapshotHeight()/infoBox.snapshotRect.height();
	int width = snapshotThread->snapshotWidth() / scale;
	if(infoBox.snapshotRect.x() + width > displayRect.width())
		width = displayRect.width() - infoBox.snapshotRect.x();
	QRectF srcRect(0, 0, snapshotThread->snapshotWidth(), snapshotThread->snapshotHeight());
	QRectF dstRect(infoBox.snapshotRect.x(), infoBox.snapshotRect.y(), width, infoBox.snapshotRect.height());
	rgaDrawImage((uchar *)snapshotThread->snapshotBuf(), RK_FORMAT_RGB_888, srcRect,
					snapshotThread->snapshotWidth(), snapshotThread->snapshotHeight(),
					image->bits(), rgaFormat, dstRect,
					image->width(), image->height(), 0, blend);
}

bool VideoItem::drawInfoBox(QPainter *painter, QImage *image)
{
	int flags;
	QFont font;
	bool blackList = false;
	char name[NAME_LEN];

	if(facial.boxRect.isEmpty())
		return blackList;

	flags = Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap;
	painter->setPen(QPen(Qt::white, 2));

#ifdef QT_FB_DRM_RGB565
	painter->setBrush(QColor(153, 51, 250, 100));
	painter->setClipRect(boundingRect());
	painter->drawRect(infoBox.infoRect);
#endif

	if(strlen(ip)) {
		font.setPixelSize(20);
		painter->setFont(font);
		painter->drawText(infoBox.ipRect, flags, QString(ip));
	}

	font.setPixelSize(40);
	font.setBold(true);
	painter->setFont(font);
	painter->drawText(infoBox.titleRect, flags, infoBox.title);

	QDateTime time = QDateTime::currentDateTime();
	QString date = time.toString("yyyy.MM.dd hh:mm:ss");
	//QString date = time.toString("yyyy.MM.dd hh:mm:ss ddd");
	font.setPixelSize(35);
	painter->setFont(font);
	painter->drawText(infoBox.timeRect, flags, date);

	blackList = findName(facial.fullName, name, NAME_LEN);
	if(strlen(name))
		painter->drawText(infoBox.nameRect, flags, QString(name));

	drawSnapshot(painter, image);

	return blackList;
}

void VideoItem::drawBox(QPainter *painter, bool blackList)
{
	int w, h;

	if(facial.boxRect.isEmpty())
		return;

	w = facial.boxRect.right() - facial.boxRect.left();
	h = facial.boxRect.bottom() - facial.boxRect.top();

	if(!strlen(facial.fullName))
		painter->setPen(QPen(Qt::red, 4));
	else if(blackList)
		painter->setPen(QPen(Qt::black, 4));
	else if(facial.real)
		painter->setPen(QPen(Qt::green, 4));
	else
		painter->setPen(QPen(Qt::yellow, 4));

	painter->setBrush(QColor(255, 255, 255, 0));
	painter->drawRect(facial.boxRect.left(), facial.boxRect.top(), w, h);
}

void VideoItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
						QWidget *widget)
{
	bool blackList = false;
	static int printf_cnt = 1;

	if(!painter->paintEngine())
		return;

	Q_UNUSED(option);
	Q_UNUSED(widget);

	mutex.lock();

#if 0
	//printf fps
	static QTime paint_time;
	static int paint_frames = 0;
	if (!paint_frames)
		paint_time.start();

	if(paint_time.elapsed()/1000 >= 10) {
		paint_frames = 0;
		paint_time.restart();
	}

	if(!(++paint_frames % 50))
		printf("----- %s FPS: %2.2f (%u frames in %ds)\n",
				__func__, paint_frames * 1000.0 / paint_time.elapsed(),
				paint_frames, paint_time.elapsed() / 1000);
#endif

	QImage *image = static_cast<QImage *>(painter->paintEngine()->paintDevice());
	if (image->isNull()) {
		mutex.unlock();
		return;
	}

	// TODO: Parse dst format
	if(printf_cnt) {
		qDebug("image->format: %d", image->format());
		printf_cnt--;
	}

	QRectF srcRect(0, 0, video.width, video.height);
	QRectF dstRect(0, 0, image->width(), image->height());

#ifdef QT_FB_DRM_RGB565
	rgaDrawImage(video.buf, video.format, srcRect, video.width, video.height,
					image->bits(), rgaFormat, dstRect, image->width(),
					image->height(), video.rotate, 0);
#else
	rgaDrawImage(video.buf, video.format, srcRect, video.width, video.height,
					image->bits(), rgaFormat, dstRect, image->width(),
					image->height(), video.rotate, 0);

	if(!facial.boxRect.isEmpty() && infoBoxBuf) {
		QRectF srcInfo(0, 0, infoBox.infoRect.width(), infoBox.infoRect.height());
		rgaDrawImage((uchar *)infoBoxBuf, rgaFormat, srcInfo,
						infoBox.infoRect.width(), infoBox.infoRect.height(),
						image->bits(), rgaFormat, infoBox.infoRect,
						image->width(), image->height(), 0, blend);
	}
#endif

	blackList = drawInfoBox(painter, image);
	drawBox(painter, blackList);

	video.buf = NULL;
	mutex.unlock();
}
