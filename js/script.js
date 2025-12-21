function setColors()
{
	if (localStorage.getItem("darkmode") === "1")
	{
		document.getElementById("darkmodeButton").innerHTML = "SAD";
		document.body.style.setProperty('--secondary-color',"var(--dark-color)");
		document.body.style.setProperty('--secondary-tint-color',"var(--dark-tint-color)");
	}
	else
	{
		document.getElementById("darkmodeButton").innerHTML = "HAPPY";
		document.body.style.setProperty('--secondary-color',"var(--light-color)");
		document.body.style.setProperty('--secondary-tint-color',"var(--light-tint-color)");
	}
}

function triggerDarkMode()
{
	document.body.classList.add("smooth");
	document.getElementsByClassName("nav")[0].classList.add("smooth");
	if (localStorage.getItem("darkmode") === "1") localStorage.setItem("darkmode", "0");
	else localStorage.setItem("darkmode", "1");
	setColors();
}

function animationSetup(animationClass)
{
	document.querySelectorAll('[class*="' + animationClass + '-"]').forEach(el =>
	{
		el.classList.add(animationClass);
		el.classList.forEach(className => 
		{
			const regex = new RegExp(`^${animationClass}-(\\d+(?:\\.\\d+)?)$`);
			const match = className.match(regex);
			if (match)
			{
				const delay = parseFloat(match[1]);
				el.style.animationDelay = `${delay}s`;
			}
		});
		el.addEventListener('animationstart', () => {el.dataset.animationStarted = 'true';});
		el.addEventListener('animationend', () => 
		{
			el.dataset.animationCompleted = 'true';
			el.classList.remove("disabled");
			if (el.dataset.bsToggleOriginal != 'none') el.setAttribute('data-bs-toggle', el.dataset.bsToggleOriginal);
		});
		el.addEventListener('click', (e) => {if (el.classList.contains('disabled')) e.preventDefault();});
		el.classList.add("disabled");
		if (el.hasAttribute('data-bs-toggle'))
		{
			el.dataset.bsToggleOriginal = el.getAttribute('data-bs-toggle');
			el.removeAttribute('data-bs-toggle');
		}
		else
		{
			el.dataset.bsToggleOriginal = 'none';
		}
	});
}

function skipAnimations(animationClass)
{
	document.querySelectorAll('[class*="' + animationClass + '-"]').forEach(el =>
	{
		if (el.dataset.animationStarted != 'true')
		{
			el.classList.forEach(className => 
			{
				const regex = new RegExp(`^${animationClass}-(\\d+(?:\\.\\d+)?)$`);
				const match = className.match(regex);
				if (match)
				{
					const originalDelay = parseFloat(match[1]);
					const newDelay = originalDelay * 0.1;

					el.style.animation = 'none';
					void el.offsetWidth;
					el.style.animation = '';

					el.style.animationDelay = `${newDelay}s`;
				}
			});
			el.classList.remove("disabled");
		}
	});
}

let hasClicked = false;
function skipIntro()
{
	if (hasClicked) return;
	hasClicked = true;

	skipAnimations("fade-in");
	skipAnimations("float-in");
}

/*window.addEventListener("load", () => 
{
	animationSetup("fade-in");
	animationSetup("float-in");

	if (sessionStorage.getItem(window.location.href) == "1") skipIntro();
	sessionStorage.setItem(window.location.href, "1");
});*/

document.addEventListener('click', () => 
{
	skipIntro();
});

setColors();

animationSetup("fade-in");
animationSetup("float-in");

if (sessionStorage.getItem(window.location.href) == "1") skipIntro();
sessionStorage.setItem(window.location.href, "1");

document.querySelectorAll("*").forEach(el =>
{
	el.classList.add("visible");
});