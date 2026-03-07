FROM alpine:latest

RUN apk add --no-cache gcc musl-dev sqlite-dev openssl-dev make

WORKDIR /app
COPY . .
RUN make

EXPOSE 7000
CMD ["./cgiscord"]
