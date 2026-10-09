// @ts-check
import { execFileSync } from 'node:child_process';
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// Detent has no release tags yet, so the header pill names the commit the docs
// were built from instead of a version. Falls back to "dev" outside a git checkout.
function gitRevision() {
	try {
		return execFileSync('git', ['rev-parse', '--short=7', 'HEAD'], { encoding: 'utf8' }).trim();
	} catch {
		return 'dev';
	}
}
process.env.PUBLIC_DETENT_REVISION = gitRevision();

import kronuzDark from './src/styles/kronuz-dark.json';
import kronuzLight from './src/styles/kronuz-light.json';
import { sidebar } from './src/lib/sidebar.mjs';
import { restLangs } from './src/plugins/rest-lang.mjs';
import remarkAnsi from './src/plugins/remark-ansi.mjs';
import remarkHints from './src/plugins/remark-hints.mjs';
import remarkD2 from './src/plugins/remark-d2.mjs';
import { pluginRestCurl } from './src/plugins/ec-rest-curl.mjs';
import { SITE_TITLE, SITE_DESCRIPTION, REPO_URL } from './src/consts';
import rehypeExternalLinks from 'rehype-external-links';

// Detent documentation, on Astro + Starlight with the Kronuz theme shared with the
// Xapiand docs and the blog. Search is Starlight's built-in Pagefind (local, static).
export default defineConfig({
	site: 'https://kronuz.github.io',
	base: '/Detent',
	// Hint styles (tip/info/caution/warning/unimplemented/construction) render as
	// custom `:::hint[Title]{.type}` asides; see src/plugins/remark-hints.mjs + custom.css.
	markdown: {
		// remarkD2 turns ```d2 fences into themed (light+dark) diagrams at build time
		// by shelling out to the d2 CLI (must be on PATH); see src/lib/renderD2.mjs.
		remarkPlugins: [remarkAnsi, remarkHints, remarkD2],
		// Open external (http/https) links in a new tab; internal links are relative,
		// so they're untouched and keep navigating in place.
		rehypePlugins: [
			[rehypeExternalLinks, { target: '_blank', rel: ['noopener', 'noreferrer'], protocols: ['http', 'https'] }],
		],
	},
	vite: {
		server: {
			allowedHosts: true,
		},
	},
	integrations: [
		starlight({
			title: SITE_TITLE,
			description: SITE_DESCRIPTION,
			logo: { src: './src/assets/logo.svg', alt: 'Detent' },
			favicon: '/favicon.svg',
			customCss: ['./src/styles/custom.css'],
			// The landing page uses the same typefaces as the Xapiand home (Lato body,
			// Montserrat display).
			head: [
				{ tag: 'link', attrs: { rel: 'preconnect', href: 'https://fonts.gstatic.com', crossorigin: true } },
				{
					tag: 'link',
					attrs: {
						rel: 'stylesheet',
						href: 'https://fonts.googleapis.com/css2?family=Lato:ital,wght@0,300;0,400;0,700;1,300;1,400;1,700&family=Montserrat:wght@800&display=swap',
					},
				},
			],
			editLink: {
				baseUrl: `${REPO_URL}/edit/main/website/`,
			},
			// ```rest blocks copy as curl against a local distributor on its default port.
			expressiveCode: {
				themes: [kronuzDark, kronuzLight],
				shiki: { langs: restLangs },
				plugins: [pluginRestCurl({ host: 'http://localhost:8317' })],
			},
			social: [{ icon: 'github', label: 'GitHub', href: REPO_URL }],
			sidebar,
			routeMiddleware: './src/starlightRouteData.ts',
			components: {
				SiteTitle: './src/components/SiteTitle.astro',
				SocialIcons: './src/components/SocialIcons.astro',
				PageTitle: './src/components/PageTitle.astro',
				EditLink: './src/components/EditLink.astro',
				Sidebar: './src/components/Sidebar.astro',
				Pagination: './src/components/Pagination.astro',
				Footer: './src/components/Footer.astro',
			},
		}),
	],
});
