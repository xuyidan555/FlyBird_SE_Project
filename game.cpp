#include "game.h"
#include <QKeyEvent>
#include <QGraphicsTextItem>
#include <QIcon>

Game::Game(QWidget* parent) : QGraphicsView(parent), score(0),isGameOver(false) {
	scene = new QGraphicsScene(this);
	setScene(scene);

    setWindowTitle("Ikun牌小鸟");

    QIcon icon(":/assets/images/bluebird-upflap.png");
    setWindowIcon(icon);

	bird = new Bird();
	scene->addItem(bird);

	// 定时器，控制游戏循环
	timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &Game::gameLoop);
	timer->start(20);

	setFixedSize(400, 600);
	scene->setSceneRect(0, 0, 400, 600);

	scene->setBackgroundBrush(QBrush(QImage(":/assets/images/background-day.png").scaled(400, 600)));

	// 取消滚动条
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	// 创建并显示分数文本项
	scoreText = new QGraphicsTextItem(QString("Score: %1").arg(score));
	//放在最前面
	scoreText->setZValue(1);
	scoreText->setDefaultTextColor(Qt::white);
	scoreText->setFont(QFont("Arial", 20));
	scoreText->setPos(10, 10);
	scene->addItem(scoreText);
}

void Game::keyPressEvent(QKeyEvent* event) {
	if (event->key() == Qt::Key_Space) {
		if (isGameOver) {
			restartGame();  // 如果游戏结束，按空格键重置游戏
		}
		else {
			bird->flap();  // 如果游戏在进行，按空格键让小鸟跳跃
		}
	}
}

void Game::restartGame()
{
	// 清除场景中的管道和文本
	for (Pipe* pipe : pipes) {
		scene->removeItem(pipe);
		delete pipe;
	}
	pipes.clear();

	// 重置小鸟的位置和状态
	bird->setPos(100, 300);
	bird->reset();

	// 重置分数
	score = 0;
	scoreText->setPlainText(QString("Score: %1").arg(score));

	// 移除 Game Over 画面
	QList<QGraphicsItem*> items = scene->items();
	for (QGraphicsItem* item : items) {
		if (QGraphicsPixmapItem* pixmapItem = dynamic_cast<QGraphicsPixmapItem*>(item)) 
		{
			if (pixmapItem->pixmap().cacheKey() == QPixmap(":/assets/images/gameover.png").cacheKey()) 
			{
				scene->removeItem(pixmapItem);
				delete pixmapItem;
			}
		}
		if (QGraphicsTextItem* textItem = dynamic_cast<QGraphicsTextItem*>(item)) {
			if (textItem->toPlainText() == "按空格键重新开始") {
				scene->removeItem(textItem);
				delete textItem;
			}
		}	
	}

	// 重置游戏状态
	isGameOver = false;
	timer->start(20);
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
			QGraphicsPixmapItem* gameOverItem = scene->addPixmap(QPixmap(":/assets/images/gameover.png"));
			// 将 Game Over 画面放在中间位置
			gameOverItem->setPos(this->width() / 2 - gameOverItem->pixmap().width() / 2, this->height() / 2 - gameOverItem->pixmap().height() / 2);
			isGameOver = true;
            //提示按空格重新游戏，用QGraphicsTextItem
            QGraphicsTextItem* restartText = new QGraphicsTextItem("按空格键重新开始");
			restartText->setDefaultTextColor(Qt::black);
			restartText->setFont(QFont("Arial", 12, QFont::Bold));
			//放在中间
			restartText->setPos(this->width() / 2 - restartText->boundingRect().width() / 2, this->height() / 2 + gameOverItem->pixmap().height() / 2 + 10);
           
            scene->addItem(restartText);
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
