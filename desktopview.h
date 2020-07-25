#ifndef DESKTOPVIEW_H
#define DESKTOPVIEW_H

#include <QGraphicsView>
#include <QGroupBox>
#include <QPushButton>
#include <QTouchEvent>
#include <rkfacial/rkfacial.h>

#include "videoitem.h"

typedef enum {
	ISP,
	CIF
} CAMERA_TYPE;

class DesktopView : public QGraphicsView
{
	Q_OBJECT

public:
	static DesktopView *desktopView;

	explicit DesktopView(int faceCnt, QWidget *parent = 0);
	virtual ~DesktopView();

protected:
	bool event(QEvent *event) override;

private:
	QGroupBox *groupBox;
	QPushButton *switchBtn;
	QPushButton *registerBtn;
	QPushButton *deleteBtn;
	QPushButton *saveBtn;

	int saveFrames;
	bool saving;

	QRect desktopRect;
	VideoItem *videoItem;

	CAMERA_TYPE cameraType;
	QTimer *timer;

#ifdef BUILD_TEST
	QGroupBox *testGroupBox;
	QPushButton *collectBtn;
	QPushButton *realBtn;
	QPushButton *photoBtn;

	bool testing;
	void initTestUi();
	static void paintTestInfo(struct test_result *test);
#endif

	void initUi();
	void initTimer();
	void iniSignalSlots();

	int initRkfacial(int faceCnt);
	void deinitRkfacial();

	void saveFile(uchar *buf, int len, uchar *flag);
	void updateUi();

	static void paintBox(int left, int top, int right, int bottom);
	static void paintInfo(struct user_info *info, bool real);

	static void displayIsp(void *src_ptr, int src_fd, int src_fmt, int src_w, int src_h, int rotation);
	static void displayCif(void *src_ptr, int src_fd, int src_fmt, int src_w, int src_h, int rotation);

private slots:
	void timerTimeOut();

	void cameraSwitch();
	void registerSlots();
	void deleteSlots();
	void saveSlots();

#ifdef BUILD_TEST
	void saveAllSlots();
	void saveRealSlots();
	void saveFakeSlots();
#endif
};

#endif // DESKTOPVIEW_H
