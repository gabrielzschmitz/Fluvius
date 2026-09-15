FROM nginx:alpine
COPY dist-web/ /usr/share/nginx/html/
EXPOSE 80