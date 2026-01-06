#include "game.h"
#include <QBrush>
#include <QDebug>
#include <QFileDialog>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsTextItem>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr int kViewWidth = 400;
constexpr int kViewHeight = 600;
constexpr int kBirdSelectorY = 430;
constexpr int kBirdSelectorStartX = 90;
constexpr int kBirdSelectorSpacing = 90;
constexpr int kBaseSkinCount = 3;
const QSize kSkinPixmapSize(40, 40);
const QSize kCustomButtonPreviewSize(58, 58);

QPixmap loadPixmapWithFallback(const QString& resourcePath) {
	QPixmap pix(resourcePath);
	if (!pix.isNull()) {
		return pix;
	}
	QString fallbackPath = resourcePath;
	if (fallbackPath.startsWith(":/")) {
		fallbackPath = fallbackPath.mid(2);
	}
	QPixmap diskPixmap(fallbackPath);
	if (!diskPixmap.isNull()) {
		qWarning() << "Resource missing in qrc, loaded from disk:" << fallbackPath;
		return diskPixmap;
	}
	qWarning() << "Failed to load pixmap from" << resourcePath;
	return QPixmap();
}

QPixmap scaledSkinPixmap(const QString& resourcePath, const QPixmap& fallback) {
	QPixmap pix = loadPixmapWithFallback(resourcePath);
	if (pix.isNull()) {
		return fallback;
	}
	return pix.scaled(kSkinPixmapSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QPixmap makeRoundedButtonPreview(const QPixmap& pix, const QSize& size = kCustomButtonPreviewSize) {
	if (pix.isNull()) {
		return QPixmap();
	}
	const QSize canvasSize = size.isEmpty() ? kCustomButtonPreviewSize : size;
	QPixmap canvas(canvasSize);
	canvas.fill(Qt::transparent);
	QPixmap scaled = pix.scaled(canvasSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
	QPainter painter(&canvas);
	painter.setRenderHint(QPainter::Antialiasing);
	QPainterPath path;
	path.addEllipse(canvas.rect());
	painter.setClipPath(path);
	const QPoint offset((canvas.width() - scaled.width()) / 2, (canvas.height() - scaled.height()) / 2);
	painter.drawPixmap(offset, scaled);
	painter.end();
	return canvas;
}
}

Game::Game(QWidget* parent)
	: QGraphicsView(parent),
	  gameOverBanner(nullptr),
	  restartHint(nullptr),
	  selectedSkinIndex(0),
	  score(0),
	  isGameOver(false),
	  startScreenVisible(false),
	  startBg(nullptr),
	  startGetReady(nullptr),
	  startGuide(nullptr),
	  startGoButton(nullptr),
	  customSkinButton(nullptr),
	  customSkinPreview(nullptr),
	  customSkinIndex(-1) {
	scene = new QGraphicsScene(this);
	setScene(scene);

	setWindowTitle("Hust牌小鸟");
	setFixedSize(kViewWidth, kViewHeight);
	scene->setSceneRect(0, 0, kViewWidth, kViewHeight);
	scene->setBackgroundBrush(QBrush(QImage(":/assets/images/background-day.png").scaled(kViewWidth, kViewHeight)));

	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	QIcon icon(":/assets/images/bluebird-upflap.png");
	setWindowIcon(icon);

	bird = new Bird();
	scene->addItem(bird);

	// 定时器，控制游戏循环（首屏 GO 前不启动）
	timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &Game::gameLoop);

	// 分数显示
	scoreText = new QGraphicsTextItem(QString("Score: %1").arg(score));
	scoreText->setZValue(1);
	scoreText->setDefaultTextColor(Qt::white);
	scoreText->setFont(QFont("Arial", 20));
	scoreText->setPos(10, 10);
	scene->addItem(scoreText);

	// 预置三种皮肤：默认 + 两个新鸟（放 assets/images/bird-yellow.png、bird-red.png）
	QPixmap basePixmap = loadPixmapWithFallback(":/assets/images/bluebird-midflap.png");
	if (basePixmap.isNull()) {
		basePixmap = QPixmap(kSkinPixmapSize);
		basePixmap.fill(Qt::blue);
	}
	const QPixmap baseSkin = basePixmap.scaled(kSkinPixmapSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	auto scaledOrBase = [&](const QString& path) {
		return scaledSkinPixmap(path, baseSkin);
	};
	skinOptions.append(baseSkin);
	skinOptions.append(scaledOrBase(":/assets/images/bird-yellow.png"));
	skinOptions.append(scaledOrBase(":/assets/images/bird-red.png"));
	bird->useDefaultSkin();

	showStartScreen();
}

void Game::keyPressEvent(QKeyEvent* event) {
	if (startScreenVisible) {
		QGraphicsView::keyPressEvent(event);
		return;
	}

	if (event->key() == Qt::Key_Space) {
		if (isGameOver) {
			restartGame();
		} else {
			bird->flap();
		}
		return;
	}

	if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
		if (isGameOver) {
			resetSkinOptionsToDefault();
			resetGameplay(false);
			showStartScreen();
		}
		return;
	}

	QGraphicsView::keyPressEvent(event);
}

void Game::mousePressEvent(QMouseEvent* event)
{
	QPointF scenePos = mapToScene(event->pos());
	if (startScreenVisible) {
		if (startGoButton && startGoButton->contains(startGoButton->mapFromScene(scenePos))) {
			startFromGoButton();
			return;
		}
		for (auto* selector : skinButtons) {
			if (selector && selector->contains(selector->mapFromScene(scenePos))) {
				applySkinIndex(selector->data(1).toInt());
				return;
			}
		}
		if (customSkinButton && customSkinButton->contains(customSkinButton->mapFromScene(scenePos))) {
			if (customSkinIndex >= 0 && event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ControlModifier)) {
				applySkinIndex(customSkinIndex);
			}
			else {
				uploadCustomSkin();
			}
			return;
		}
		return;
	}

	if (!isGameOver) {
		bird->flap();
	}

	QGraphicsView::mousePressEvent(event);
}

void Game::restartGame()
{
	resetGameplay(true);
}

void Game::gameLoop() {
	bird->updatePosition();

	// 生成新的管道
	if (pipes.isEmpty() || pipes.last()->x() < 200) {
		Pipe* pipe = new Pipe();
		pipes.append(pipe);
		scene->addItem(pipe);
	}

	// 管道移动与检测碰撞
	auto it = pipes.begin();
	while (it != pipes.end()) {
		Pipe* pipe = *it;
		pipe->movePipe();

		// 检测与小鸟的碰撞
		if (bird->collidesWithItem(pipe)) {
			timer->stop();
			if (!gameOverBanner) {
				gameOverBanner = scene->addPixmap(QPixmap(":/assets/images/gameover.png"));
				gameOverBanner->setPos(this->width() / 2 - gameOverBanner->pixmap().width() / 2, this->height() / 2 - gameOverBanner->pixmap().height() / 2);
			}
			if (!restartHint) {
				restartHint = new QGraphicsTextItem("按空格键重新开始\n回车返回开始界面");
				restartHint->setDefaultTextColor(QColor("#fef6d8"));
				restartHint->setFont(QFont("Microsoft YaHei", 15, QFont::DemiBold));
				restartHint->setPos(this->width() / 2 - restartHint->boundingRect().width() / 2, this->height() / 2 + gameOverBanner->pixmap().height() / 2 + 14);
				restartHint->setZValue(1);
				auto* shadow = new QGraphicsDropShadowEffect();
				shadow->setBlurRadius(28);
				shadow->setOffset(0, 4);
				shadow->setColor(QColor(0, 0, 0, 180));
				restartHint->setGraphicsEffect(shadow);
				scene->addItem(restartHint);
			}
			isGameOver = true;
			return;
		}

		// 如果小鸟通过了管道（即小鸟的x坐标刚好超过管道的x坐标）
		if (pipe->x() + pipe->boundingRect().width() < bird->x() && !pipe->isPassed) {
			// 增加分数
			score++;
			pipe->isPassed = true;  // 确保不会重复加分

			// 更新分数显示
			scoreText->setPlainText(QString("Score: %1").arg(score));
		}

		// 如果管道移出了屏幕，将其从场景和列表中删除
		if (pipe->x() < -60) {
			scene->removeItem(pipe);
			delete pipe;
			it = pipes.erase(it);  // 从列表中安全移除管道
		}
		else {
			++it;  // 继续遍历
		}
	}
}

void Game::resetGameplay(bool startImmediately)
{
	for (Pipe* pipe : pipes) {
		scene->removeItem(pipe);
		delete pipe;
	}
	pipes.clear();

	if (gameOverBanner) {
		scene->removeItem(gameOverBanner);
		delete gameOverBanner;
		gameOverBanner = nullptr;
	}
	if (restartHint) {
		scene->removeItem(restartHint);
		delete restartHint;
		restartHint = nullptr;
	}

	bird->reset();

	score = 0;
	scoreText->setPlainText(QString("Score: %1").arg(score));

	isGameOver = false;
	timer->stop();
	if (startImmediately) {
		timer->start(20);
	}
}

void Game::showStartScreen()
{
	if (startScreenVisible) return;
	timer->stop();
	startScreenVisible = true;

	startBg = scene->addPixmap(QPixmap(":/assets/images/background-day.png").scaled(kViewWidth, kViewHeight));
	startBg->setZValue(2);

	startGetReady = scene->addPixmap(QPixmap(":/assets/images/start-getready.png"));
	if (startGetReady && !startGetReady->pixmap().isNull()) {
		startGetReady->setPos((kViewWidth - startGetReady->pixmap().width()) / 2, 60);
		startGetReady->setZValue(3);
	}

	startGuide = scene->addPixmap(QPixmap(":/assets/images/start-guide.png"));
	if (startGuide && !startGuide->pixmap().isNull()) {
		startGuide->setPos((kViewWidth - startGuide->pixmap().width()) / 2, 170);
		startGuide->setZValue(3);
	}

	startGoButton = scene->addPixmap(QPixmap(":/assets/images/start-go.png"));
	if (startGoButton) {
		if (startGoButton->pixmap().isNull()) {
			QPixmap fallback(140, 50);
			fallback.fill(Qt::darkYellow);
			startGoButton->setPixmap(fallback);
		}
		startGoButton->setPos((kViewWidth - startGoButton->pixmap().width()) / 2, 305);
		startGoButton->setZValue(4);
	}

	buildSkinSelectors();
}

void Game::hideStartScreen()
{
	startScreenVisible = false;
	for (auto item : {startBg, startGetReady, startGuide, startGoButton}) {
		if (item) {
			scene->removeItem(item);
			delete item;
		}
	}
	startBg = startGetReady = startGuide = startGoButton = nullptr;

	for (auto* selector : skinButtons) {
		scene->removeItem(selector);
		delete selector;
	}
	skinButtons.clear();

	if (customSkinButton) {
		scene->removeItem(customSkinButton);
		delete customSkinButton;
		customSkinButton = nullptr;
	}
	if (customSkinPreview) {
		scene->removeItem(customSkinPreview);
		delete customSkinPreview;
		customSkinPreview = nullptr;
	}
}

void Game::buildSkinSelectors()
{
	for (auto* selector : skinButtons) {
		scene->removeItem(selector);
		delete selector;
	}
	skinButtons.clear();
	if (customSkinPreview) {
		scene->removeItem(customSkinPreview);
		delete customSkinPreview;
		customSkinPreview = nullptr;
	}

	const int displayCount = qMin(kBaseSkinCount, skinOptions.size());
	for (int index = 0; index < displayCount; ++index) {
		const QPixmap& skin = skinOptions[index];
		QGraphicsPixmapItem* selector = scene->addPixmap(skin);
		selector->setPos(kBirdSelectorStartX + index * kBirdSelectorSpacing, kBirdSelectorY);
		selector->setZValue(4);
		selector->setData(0, "skin");
		selector->setData(1, index);
		selector->setScale(index == selectedSkinIndex ? 1.2 : 1.0);
		skinButtons.append(selector);
	}

	// 自定义上传按钮
	if (customSkinButton) {
		scene->removeItem(customSkinButton);
		delete customSkinButton;
		customSkinButton = nullptr;
	}
	QPixmap customPixmap = QPixmap(":/assets/images/skin-custom.png").scaled(58, 58, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	if (customPixmap.isNull()) {
		customPixmap = QPixmap(kCustomButtonPreviewSize);
		customPixmap.fill(Qt::gray);
	}
	customSkinButton = scene->addPixmap(customPixmap);
	customSkinButton->setPos(kBirdSelectorStartX + 1 * kBirdSelectorSpacing, kBirdSelectorY + 68);
	customSkinButton->setZValue(4);
	customSkinButton->setData(0, "custom");
	updateCustomSkinPreview();
}

void Game::updateCustomSkinPreview()
{
	if (customSkinPreview) {
		scene->removeItem(customSkinPreview);
		delete customSkinPreview;
		customSkinPreview = nullptr;
	}
	if (!customSkinButton) {
		return;
	}
	if (customSkinIndex < 0 || customSkinIndex >= skinOptions.size()) {
		return;
	}
	QSize targetSize = customSkinButton->pixmap().isNull() ? kCustomButtonPreviewSize : customSkinButton->pixmap().size();
	QPixmap preview = makeRoundedButtonPreview(skinOptions[customSkinIndex], targetSize);
	if (preview.isNull()) {
		return;
	}
	customSkinPreview = scene->addPixmap(preview);
	customSkinPreview->setPos(customSkinButton->pos());
	customSkinPreview->setZValue(customSkinButton->zValue() + 0.2);
	customSkinPreview->setAcceptedMouseButtons(Qt::NoButton);
}

void Game::applySkinIndex(int index)
{
	if (index < 0 || index >= skinOptions.size()) return;
	selectedSkinIndex = index;
	if (index == 0) {
		bird->useDefaultSkin();
	} else {
		bird->setSkin(skinOptions[index]);
	}
	for (auto* selector : skinButtons) {
		if (selector) {
			selector->setScale(selector->data(1).toInt() == selectedSkinIndex ? 1.2 : 1.0);
		}
	}
}

void Game::startFromGoButton()
{
	hideStartScreen();
	resetGameplay(true);
}

void Game::uploadCustomSkin()
{
	QString path = QFileDialog::getOpenFileName(this, "选择自定义小鸟", QString(), "Images (*.png *.jpg *.jpeg)");
	if (path.isEmpty()) return;
	QPixmap pix(path);
	if (pix.isNull()) return;

	QPixmap scaled = pix.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    if (customSkinIndex < 0) {
        customSkinIndex = skinOptions.size();
        skinOptions.append(scaled);
    } else {
        skinOptions[customSkinIndex] = scaled;
    }
    selectedSkinIndex = customSkinIndex;
    bird->setSkin(scaled);

	buildSkinSelectors();
}

void Game::resetSkinOptionsToDefault()
{
    // 清空自定义，并回到默认选中
    while (skinOptions.size() > 3) {
        skinOptions.removeLast();
    }
    customSkinIndex = -1;
    selectedSkinIndex = 0;
    bird->useDefaultSkin();
}