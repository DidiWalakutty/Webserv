const moveSpeed = 3.0;
const turnSpeed = 0.05;
const mouseTurnSpeed = 0.1;
const lineSize = 1.0;
const ballSize = 5.0;
const reactionTime = 0.0025;

const cursor = 
{
	x: window.innerWidth / 2,
	y: window.innerHeight / 2,
	pressed: false
};

class line
{
	constructor(cx, cy, dx, dy)
	{
		this.cx = centerWidth();
		this.cy = centerHeight();
		this.dx = 1;
		this.dy = -1;
		this.moveSpeed = 1.0;
		this.turnSpeed = 1.0;
		this.size = 1.0;
		this.mouseStrength = 0.0;
		this.turnStrength = 0.0;
		this.bounciness = 1.0;
		this.gravity = 1.0;
		this.radius = 1.0;
	}

	move()
	{
		this.cx += this.dx * moveSpeed * this.moveSpeed;
		this.cy += this.dy * moveSpeed * this.moveSpeed;

		this.checkBounds();

		let cdx;
		let cdy;

		if (cursor.pressed)
		{
			this.mouseStrength = clamp(this.mouseStrength + reactionTime * this.turnSpeed, 0.0, 1.0);
			this.turnStrength = clamp(this.turnStrength - reactionTime * this.turnSpeed, 0.0, 1.0);

			cdx = cursor.x - this.cx;
			cdy = cursor.y - this.cy;

			this.dx *= 0.99;
			this.dy *= 0.99;

			this.dx += normalize(cdx, cdy) * mouseTurnSpeed * this.turnSpeed * this.mouseStrength;
			this.dy += normalize(cdy, cdx) * mouseTurnSpeed * this.turnSpeed * this.mouseStrength;
		}
		else
		{
			this.mouseStrength = clamp(this.mouseStrength - reactionTime * this.turnSpeed, 0.0, 1.0);
			this.turnStrength = clamp(this.turnStrength + reactionTime * this.turnSpeed, 0.0, 1.0);

			/*cdx = random11();
			cdy = random11();

			this.dx += normalize(cdx, cdy) * turnSpeed * this.turnSpeed * this.turnStrength;
			this.dy += normalize(cdy, cdx) * turnSpeed * this.turnSpeed * this.turnStrength;*/
		}

		/*this.dx = clamp(this.dx, -1.0, 1.0);
		this.dy = clamp(this.dy, -1.0, 1.0);*/
		let tempx = this.dx;
		let tempy = this.dy;
		this.dx = normalize(tempx, tempy);
		this.dy = normalize(tempy, tempx);
	}

	fall()
	{
		this.cx += this.dx * this.turnSpeed;
		this.cy += this.dy * this.moveSpeed * 0.25;

		if (this.checkBounds())
		{
			if (cursor.pressed)
			{
				this.dx *= 0.25 * this.bounciness;
				this.dy *= 0.25 * this.bounciness;
			}
			else
			{
				this.dx *= 0.9 * this.bounciness;
				this.dy *= 0.9 * this.bounciness;
			}
		}

		if (cursor.pressed)
		{
			this.mouseStrength = clamp(this.mouseStrength + reactionTime, 0.0, 1.0);

			const cdx = cursor.x - this.cx;
			const cdy = cursor.y - this.cy;
			/*const xm = clamp(Math.abs(cdx) * 0.1, 0.0, 1.0);
			const ym = clamp(Math.abs(cdy) * 0.1, 0.0, 1.0);*/
			const xm = clamp((Math.abs(cdx) * 2.0) / window.innerWidth, 0.0, 1.0);
			const ym = clamp((Math.abs(cdy) * 2.0) / window.innerHeight, 1.0);

			this.dx *= 0.999;
			this.dy *= 0.999;

			this.dx += normalize(cdx, cdy) * xm * mouseTurnSpeed;
			this.dy += normalize(cdy, cdx) * ym * mouseTurnSpeed * 4.0;
		}
		else
		{
			this.mouseStrength = clamp(this.mouseStrength - reactionTime, 0.0, 1.0);
			this.dy *= this.gravity;
			this.dy += 0.25;
		}
	}

	checkBounds()
	{
		let hit = false;

		if (this.cx + this.radius >= window.innerWidth)
		{
			this.dx = -Math.abs(this.dx); 
			this.cx = window.innerWidth - this.radius;
			hit = true;
		}
		if (this.cx - this.radius <= 0)
		{
			this.dx = Math.abs(this.dx); 
			this.cx = this.radius;
			hit = true;
		}
		if (this.cy + this.radius >= window.innerHeight)
		{
			this.dy = -Math.abs(this.dy); 
			this.cy = window.innerHeight - this.radius;
			hit = true;
		}
		if (this.cy - this.radius <= 0)
		{
			this.dy = Math.abs(this.dy); 
			this.cy = this.radius;
			hit = true;
		}

		return (hit);
	}
};

const lines = [];

const canvas = document.createElement("canvas");
canvas.setAttribute("width", window.innerWidth);
canvas.setAttribute("height", window.innerHeight);
canvas.classList.add("visible");
canvas.classList.add("instant");
document.body.append(canvas);

const ctx = canvas.getContext("2d");
ctx.lineWidth = 5;
/*ctx.strokeStyle ="rgb(13, 110, 253, 0.1)";*/
ctx.strokeStyle ="rgb(13, 110, 253, 1.0)";
ctx.fillStyle ="rgb(13, 110, 253, 1.0)";
ctx.globalAlpha = 1.0;

setup(1000);

/*setInterval(frame, 1);*/

function resizeContext()
{
	canvas.setAttribute("width", window.innerWidth);
	canvas.setAttribute("height", window.innerHeight);
	ctx.fillStyle ="rgb(13, 110, 253, 1.0)";
}

function centerWidth()
{
	return (window.innerWidth * 0.5);
}

function centerHeight()
{
	return (window.innerHeight * 0.5);
}

function random11()
{
	return ((Math.random() - 0.5) * 2.0);
}

function random(min, max)
{
	return (inter(min, max, Math.random()));
}

function inter(a, b, i)
{
	return (((b - a) * i) + a);
}

function clamp(val, min, max)
{
	if (val < min) return (min);
	if (val > max) return (max);
	return (val);
}

function normalize(a, b)
{
	return (a / (Math.abs(a) + Math.abs(b)));
}

function setup(amount)
{

	for (let i = 0; i < amount; i++)
	{
		lines[i] = new line();
		lines[i].dx = random11() * 7.5;
		/*lines[i].dy = random11()*/;
		lines[i].dy = Math.random() * -42.5;
		lines[i].moveSpeed = 1.0 + (random11() * 0.5);
		lines[i].turnSpeed = 1.0 + (random11() * 0.5);
		lines[i].size = 1.0 + (random11() * 0.5);
		lines[i].bounciness = random(0.9, 0.9999);
		lines[i].gravity = random(0.99, 0.9999);
		lines[i].radius = ballSize * lines[i].size;
	}
}

function moveLine(i)
{
	ctx.lineWidth = lineSize * lines[i].size;
	ctx.beginPath();
	ctx.moveTo(lines[i].cx, lines[i].cy);

	lines[i].move();

	ctx.lineTo(lines[i].cx, lines[i].cy);
	ctx.stroke();
	ctx.closePath();
}

function bounceBall(i)
{
	lines[i].fall();

	ctx.beginPath();

	ctx.arc(lines[i].cx, lines[i].cy, lines[i].radius, 0, Math.PI * 2.0, true);

	ctx.closePath();

	ctx.fill();
}

addEventListener("mousedown", (e) => 
{
	cursor.pressed = true;
});

addEventListener("mouseup", (e) => 
{
	cursor.pressed = false;
});

addEventListener("mousemove", (e) => 
{
	cursor.x = e.clientX;
	cursor.y = e.clientY;
});

addEventListener("resize", (e) => 
{
	resizeContext();
});

function frame()
{
	requestAnimationFrame(frame);
	/*ctx.fillStyle = '"' + document.body.style.getPropertyValue(document.body.style.
		getPropertyValue('--secondary-tint-color')) + '"';*/
	/*ctx.fillStyle = 'rgba(0, 0, 0, 0.01)';
  	ctx.fillRect(0, 0, window.innerWidth, window.innerHeight);*/

	ctx.clearRect(0, 0, window.innerWidth, window.innerHeight);

	for (let i = 0; i < lines.length; i++)
	{
		/*moveLine(i);*/
		bounceBall(i);
	}


}

frame();