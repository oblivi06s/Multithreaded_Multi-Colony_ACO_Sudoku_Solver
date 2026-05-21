FROM python:3.11-slim

WORKDIR /app

# Install build tools for the C++ solver
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential make \
 && rm -rf /var/lib/apt/lists/*

# Copy repository into the image
COPY . /app

# Build the solver binary as described in the project docs
# Ensure object directory exists for Makefile outputs
RUN mkdir -p obj && make -f markdowns/Makefile

# Install Python dependencies for the webapp
RUN pip install --no-cache-dir -r webapp/requirements.txt

# Gunicorn will serve the Flask app on port 8000
EXPOSE 8000
ENV PYTHONUNBUFFERED=1

# webapp.app:app -> (package) webapp / (module) app / (Flask instance) app
CMD ["gunicorn", "-b", "0.0.0.0:8000", "webapp.app:app"]

