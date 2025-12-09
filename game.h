#ifndef GAME_H
#define GAME_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QTimer>
#include "bird.h"
#include "pipe.h"

class Game : public QGraphicsView {
	Q_OBJECT
public:
	Game(QWidget* parent = nullptr);
	void keyPressEvent(QKeyEvent* event);
	void restartGame();
private slots:
	void gameLoop();

private:
	QGraphicsScene* scene;
	QGraphicsTextItem* scoreText;
	Bird* bird;
	QTimer* timer;
	QList<Pipe*> pipes;
	int score;
	bool isGameOver;
};

#endif // GAME_H
