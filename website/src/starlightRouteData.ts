import { defineRouteMiddleware } from '@astrojs/starlight/route-data';

// Lead the "On this page" list with the page title instead of Starlight's generic
// "Overview". Mutating route.toc here covers both the desktop right-sidebar TOC and the
// mobile "On this page" dropdown (both read route.toc), which replaces the two component
// overrides this used to require. Mirrors the blogs' src/starlightRouteData.ts.
const READER_PREFERENCES = [
	"try{var d=document.documentElement,s=localStorage,e=new URLSearchParams(location.search).get('editor');",
	"if(s.getItem('kz-text-size')==='large')d.dataset.textSize='large';",
	"if(e==='on')s.setItem('kz-editor','1');else if(e==='off')s.removeItem('kz-editor');",
	"if(s.getItem('kz-editor'))d.dataset.editor=''}catch(x){}",
].join('');

export const onRequest = defineRouteMiddleware((context) => {
	context.locals.starlightRoute.head.push({ tag: 'script', content: READER_PREFERENCES });
	const route = context.locals.starlightRoute;
	const topItem = route?.toc?.items?.find((it) => it.slug === '_top');
	if (topItem && route.entry?.data?.title) topItem.text = route.entry.data.title;
});
