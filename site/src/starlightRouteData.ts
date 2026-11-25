import { defineRouteMiddleware } from '@astrojs/starlight/route-data';
import { readerPreferences } from '@kronuz/starlight-core/lib/readerPreferences.mjs';

// Lead the "On this page" list with the page title instead of Starlight's generic
// "Overview". Mutating route.toc here covers both the desktop right-sidebar TOC and the
// mobile "On this page" dropdown (both read route.toc), which replaces the two component
// overrides this used to require. Mirrors the blogs' src/starlightRouteData.ts.
export const onRequest = defineRouteMiddleware((context) => {
	(context.locals as typeof context.locals & { kzFooter?: unknown }).kzFooter = {
		copyrightHref: `${import.meta.env.BASE_URL.replace(/\/$/, '')}/copyright/`,
	};
	context.locals.starlightRoute.head.push({ tag: 'script', content: readerPreferences });
	const route = context.locals.starlightRoute;
	const topItem = route?.toc?.items?.find((it) => it.slug === '_top');
	if (topItem && route.entry?.data?.title) topItem.text = route.entry.data.title;
});
